#include "RepairWorker.hpp"

#include "modelrepair/Decimate.hpp"
#include "modelrepair/Remesh.hpp"
#include "modelrepair/Smooth.hpp"
#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/io/MeshIO.hpp"

#include <QMetaObject>

namespace gui
{

RepairWorker::RepairWorker(std::filesystem::path input,
                           modelrepair::RepairOptions opts,
                           QObject* parent)
    : QObject(parent), input_(std::move(input)), opts_(std::move(opts))
{}

void RepairWorker::run()
{
    modelrepair::Mesh mesh;
    try
    {
        mesh = modelrepair::io::load(input_);
    }
    catch (const std::exception& e)
    {
        emit finished({}, {}, {}, QString::fromStdString(e.what()));
        return;
    }

    modelrepair::Mesh before_mesh = mesh;  // snapshot before in-place repair

    // Compute grand total so post-repair steps appear in the same bar.
    const bool do_post = !opts_.diagnose_only;
    const int post_steps = (opts_.remesh   && do_post ? static_cast<int>(opts_.remesh_iterations) : 0)
                         + (opts_.smooth   && do_post ? static_cast<int>(opts_.smooth_iterations) : 0)
                         + (opts_.decimate && do_post ? 1 : 0);
    constexpr int pipeline_total = 6;
    const int grand_total = pipeline_total + post_steps;

    modelrepair::RepairPipeline pipeline(opts_);

    // Emit directly — Qt's auto-connection detects the cross-thread receiver
    // (MainWindow lives on the main thread) and uses QueuedConnection, posting
    // to the main event loop which processes it between mesh operations.
    // The old invokeMethod(this, lambda, QueuedConnection) queued to the worker
    // thread itself, which has no exec() loop during run() and never delivered.
    pipeline.set_progress_callback(
        [this, grand_total](int step, int /*total*/, const std::string& name)
        {
            emit progressChanged(step, grand_total, QString::fromStdString(name));
        });

    modelrepair::RepairReport report;
    try
    {
        report = pipeline.run(mesh);
    }
    catch (const std::exception& e)
    {
        emit finished({}, {}, {}, QString::fromStdString(e.what()));
        return;
    }

    int post_step = pipeline_total;

    // Post-repair remeshing (before smooth)
    if (opts_.remesh && !report.diagnose_only)
    {
        const unsigned int total_iters = opts_.remesh_iterations;
        // Bar advances one slot per iteration; emit first slot before remesh starts,
        // callback advances it for each subsequent iteration. on_progress uses
        // setValue(step - 1), so "step N starting" = N-1 done.
        emit progressChanged(post_step + 1, grand_total,
            QString("Remeshing 1/%1").arg(total_iters));
        modelrepair::RemeshResult rr;
        try
        {
            rr = modelrepair::remesh(
                mesh, opts_.remesh_edge_length_factor, total_iters,
                opts_.smooth_crease_angle,
                [this, post_step, grand_total, total_iters](unsigned int completed) {
                    if (completed < total_iters) {
                        emit progressChanged(post_step + completed + 1, grand_total,
                            QString("Remeshing %1/%2").arg(completed + 1).arg(total_iters));
                    }
                });
        }
        catch (const std::exception& e)
        {
            emit finished({}, {}, {}, QString("Remeshing failed: ") + e.what());
            return;
        }
        post_step += static_cast<int>(total_iters);
        modelrepair::StepReport sr;
        sr.name         = "Remesh";
        sr.was_run      = true;
        sr.issues_found = 0;
        sr.issues_fixed = static_cast<int>(rr.faces_after) - static_cast<int>(rr.faces_before);
        sr.duration_ms  = rr.duration_ms;
        report.steps.push_back(sr);
        report.triangles_after    = mesh.num_faces();
        report.surface_area_after = mesh.surface_area();
        report.volume_after       = mesh.volume();
    }

    // Post-repair smoothing
    if (opts_.smooth && !report.diagnose_only)
    {
        const unsigned int total_iters = opts_.smooth_iterations;
        emit progressChanged(post_step + 1, grand_total,
            QString("Smoothing 1/%1").arg(total_iters));
        modelrepair::SmoothResult smr;
        try
        {
            smr = modelrepair::smooth(
                mesh, total_iters, opts_.smooth_crease_angle,
                [this, post_step, grand_total, total_iters](unsigned int completed) {
                    if (completed < total_iters) {
                        emit progressChanged(post_step + completed + 1, grand_total,
                            QString("Smoothing %1/%2").arg(completed + 1).arg(total_iters));
                    }
                });
        }
        catch (const std::exception& e)
        {
            emit finished({}, {}, {}, QString("Smoothing failed: ") + e.what());
            return;
        }
        post_step += static_cast<int>(total_iters);
        modelrepair::StepReport sr;
        sr.name         = "Smooth";
        sr.was_run      = true;
        sr.issues_found = 0;
        sr.issues_fixed = 0;
        sr.duration_ms  = smr.duration_ms;
        report.steps.push_back(sr);
        report.surface_area_after = mesh.surface_area();
        report.volume_after       = mesh.volume();
    }

    // Post-repair decimation
    if (opts_.decimate && !report.diagnose_only)
    {
        emit progressChanged(++post_step, grand_total, "Decimating");
        modelrepair::DecimateResult dr;
        try
        {
            dr = modelrepair::decimate(mesh, opts_.decimate_ratio);
        }
        catch (const std::exception& e)
        {
            emit finished({}, {}, {}, QString("Decimation failed: ") + e.what());
            return;
        }
        modelrepair::StepReport sr;
        sr.name         = "Decimate";
        sr.was_run      = true;
        sr.issues_found = 0;
        sr.issues_fixed = dr.faces_before - dr.faces_after;
        sr.duration_ms  = dr.duration_ms;
        report.steps.push_back(sr);
        report.triangles_after    = mesh.num_faces();
        report.surface_area_after = mesh.surface_area();
        report.volume_after       = mesh.volume();
    }

    emit finished(report, std::move(before_mesh), std::move(mesh), {});
}

} // namespace gui

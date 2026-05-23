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

    modelrepair::RepairPipeline pipeline(opts_);

    // Post progress signals safely across threads.
    pipeline.set_progress_callback(
        [this](int step, int total, const std::string& name)
        {
            QMetaObject::invokeMethod(this, [this, step, total, name]()
            {
                emit progressChanged(step, total, QString::fromStdString(name));
            }, Qt::QueuedConnection);
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

    // Post-repair remeshing (before smooth)
    if (opts_.remesh && !report.diagnose_only)
    {
        modelrepair::RemeshResult rr;
        try
        {
            rr = modelrepair::remesh(mesh, opts_.remesh_edge_length_factor, opts_.remesh_iterations);
        }
        catch (const std::exception& e)
        {
            emit finished({}, {}, {}, QString("Remeshing failed: ") + e.what());
            return;
        }
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
        modelrepair::SmoothResult smr;
        try
        {
            smr = modelrepair::smooth(mesh, opts_.smooth_iterations, opts_.smooth_crease_angle);
        }
        catch (const std::exception& e)
        {
            emit finished({}, {}, {}, QString("Smoothing failed: ") + e.what());
            return;
        }
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

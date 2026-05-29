#include "WizardWorker.hpp"

#include "modelrepair/Decimate.hpp"
#include "modelrepair/Remesh.hpp"
#include "modelrepair/Smooth.hpp"
#include "modelrepair/RepairPipeline.hpp"

namespace { struct WizardCancelled {}; }

namespace gui
{

WizardWorker::WizardWorker(modelrepair::Mesh mesh, modelrepair::RepairOptions opts,
                            QObject* parent)
    : QObject(parent), phase_(Phase::Repair), mesh_(std::move(mesh)), opts_(std::move(opts))
{}

WizardWorker::WizardWorker(modelrepair::Mesh mesh,
                            bool do_remesh, double remesh_factor, unsigned int remesh_iters,
                            bool do_smooth, unsigned int smooth_iters, double crease_angle,
                            bool use_vulkan,
                            QObject* parent)
    : QObject(parent), phase_(Phase::RemeshSmooth), mesh_(std::move(mesh))
    , do_remesh_(do_remesh), remesh_factor_(remesh_factor), remesh_iters_(remesh_iters)
    , do_smooth_(do_smooth), smooth_iters_(smooth_iters), crease_angle_(crease_angle)
    , use_vulkan_(use_vulkan)
{}

WizardWorker::WizardWorker(modelrepair::Mesh mesh, double decimate_ratio, QObject* parent)
    : QObject(parent), phase_(Phase::Decimate), mesh_(std::move(mesh))
    , decimate_ratio_(decimate_ratio)
{}

void WizardWorker::run()
{
    modelrepair::RepairReport report;

    if (phase_ == Phase::Repair)
    {
        opts_.remesh   = false;
        opts_.smooth   = false;
        opts_.decimate = false;

        modelrepair::RepairPipeline pipeline(opts_);
        pipeline.set_progress_callback(
            [this](int step, int total, const std::string& name) {
                emit progressChanged(step, total, QString::fromStdString(name));
            });

        try {
            report = pipeline.run(mesh_);
        } catch (const std::exception& e) {
            emit finished(std::move(mesh_), {}, QString::fromStdString(e.what()));
            return;
        }
    }
    else if (phase_ == Phase::RemeshSmooth)
    {
        const int total = (do_remesh_ ? static_cast<int>(remesh_iters_) : 0)
                        + (do_smooth_ ? static_cast<int>(smooth_iters_)  : 0);
        int done = 0;

        try {
            if (do_remesh_) {
                emit progressChanged(done + 1, total,
                    QString("Remeshing 1/%1").arg(remesh_iters_));
                try {
                    auto rr = modelrepair::remesh(
                        mesh_, remesh_factor_, remesh_iters_, crease_angle_,
                        [this, &done, total, n = remesh_iters_](unsigned int completed) {
                            if (cancel_flag_ && cancel_flag_->load()) throw WizardCancelled{};
                            ++done;
                            emit progressChanged(done + 1, total,
                                QString("Remeshing %1/%2").arg(completed).arg(n));
                        });
                    modelrepair::StepReport sr;
                    sr.name        = "Remesh";
                    sr.was_run     = true;
                    sr.issues_fixed = rr.faces_after >= rr.faces_before
                        ? rr.faces_after - rr.faces_before : 0;
                    sr.duration_ms = rr.duration_ms;
                    report.steps.push_back(sr);
                } catch (const std::exception& e) {
                    emit finished(std::move(mesh_), {}, QString("Remeshing failed: ") + e.what());
                    return;
                }
            }

            if (do_smooth_) {
                emit progressChanged(done + 1, total,
                    QString("Smoothing 1/%1").arg(smooth_iters_));
                try {
                    auto smr = modelrepair::smooth(
                        mesh_, smooth_iters_, crease_angle_,
                        [this, &done, total, n = smooth_iters_](unsigned int completed) {
                            if (cancel_flag_ && cancel_flag_->load()) throw WizardCancelled{};
                            ++done;
                            emit progressChanged(done + 1, total,
                                QString("Smoothing %1/%2").arg(completed).arg(n));
                        },
                        use_vulkan_);
                    modelrepair::StepReport sr;
                    sr.name       = "Smooth";
                    sr.was_run    = true;
                    sr.duration_ms = smr.duration_ms;
                    report.steps.push_back(sr);
                } catch (const std::exception& e) {
                    emit finished(std::move(mesh_), {}, QString("Smoothing failed: ") + e.what());
                    return;
                }
            }
        } catch (const WizardCancelled&) {
            emit finished(std::move(mesh_), {}, "__cancelled__");
            return;
        }
    }
    else if (phase_ == Phase::Decimate)
    {
        emit progressChanged(1, 1, "Decimating");
        try {
            auto dr = modelrepair::decimate(mesh_, decimate_ratio_);
            modelrepair::StepReport sr;
            sr.name        = "Decimate";
            sr.was_run     = true;
            sr.issues_fixed = static_cast<int>(dr.faces_before) - static_cast<int>(dr.faces_after);
            sr.duration_ms = dr.duration_ms;
            report.steps.push_back(sr);
        } catch (const std::exception& e) {
            emit finished(std::move(mesh_), {}, QString("Decimation failed: ") + e.what());
            return;
        }
    }

    emit finished(std::move(mesh_), report, {});
}

} // namespace gui

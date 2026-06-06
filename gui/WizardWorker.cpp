#include "WizardWorker.hpp"

#include "modelrepair/Decimate.hpp"
#include "modelrepair/NormalMapDisplace.hpp"
#include "modelrepair/Remesh.hpp"
#include "modelrepair/Smooth.hpp"
#include "modelrepair/Subdivide.hpp"
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
                            bool do_subdivide, int subdivide_method, unsigned int subdivide_iters,
                            bool do_displace, std::string normal_map_path,
                            float displacement_strength, int pre_subdivisions, bool flip_green,
                            QObject* parent)
    : QObject(parent), phase_(Phase::RemeshSmooth), mesh_(std::move(mesh))
    , do_remesh_(do_remesh), remesh_factor_(remesh_factor), remesh_iters_(remesh_iters)
    , do_smooth_(do_smooth), smooth_iters_(smooth_iters), crease_angle_(crease_angle)
    , use_vulkan_(use_vulkan)
    , do_subdivide_(do_subdivide), subdivide_method_(subdivide_method)
    , subdivide_iters_(subdivide_iters)
    , do_displace_(do_displace), displace_normal_map_(std::move(normal_map_path))
    , displace_strength_(displacement_strength), displace_presubdiv_(pre_subdivisions)
    , displace_flip_green_(flip_green)
{}

WizardWorker::WizardWorker(modelrepair::Mesh mesh, double decimate_ratio,
                            modelrepair::DecimateBackend backend,
                            double target_error, double normal_deviation,
                            QObject* parent)
    : QObject(parent), phase_(Phase::Decimate), mesh_(std::move(mesh))
    , decimate_ratio_(decimate_ratio)
    , decimate_backend_(backend)
    , decimate_target_error_(target_error)
    , decimate_normal_dev_(normal_deviation)
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
        const int total = (do_remesh_    ? static_cast<int>(remesh_iters_)   : 0)
                        + (do_smooth_    ? static_cast<int>(smooth_iters_)   : 0)
                        + (do_subdivide_ ? 1 : 0)
                        + (do_displace_  ? 1 : 0);
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
            if (do_subdivide_) {
                if (cancel_flag_ && cancel_flag_->load()) throw WizardCancelled{};
                emit progressChanged(done + 1, total, "Subdividing");
                try {
                    auto method = (subdivide_method_ == 1)
                        ? modelrepair::SubdivisionMethod::CatmullClark
                        : modelrepair::SubdivisionMethod::Loop;
                    auto sr_sub = modelrepair::subdivide(mesh_, subdivide_iters_, method);
                    modelrepair::StepReport sr;
                    sr.name       = "Subdivide";
                    sr.was_run    = true;
                    sr.duration_ms = sr_sub.duration_ms;
                    report.steps.push_back(sr);
                    ++done;
                } catch (const std::exception& e) {
                    emit finished(std::move(mesh_), {}, QString("Subdivision failed: ") + e.what());
                    return;
                }
            }
            if (do_displace_) {
                if (cancel_flag_ && cancel_flag_->load()) throw WizardCancelled{};
                emit progressChanged(done + 1, total, "Baking normal map…");
                try {
                    modelrepair::NormalMapDisplaceParams dp;
                    dp.normal_map_path      = displace_normal_map_;
                    dp.displacement_strength = displace_strength_;
                    dp.pre_subdivisions     = displace_presubdiv_;
                    dp.flip_green           = displace_flip_green_;
                    auto dr = modelrepair::displace_from_normal_map(mesh_, dp);
                    modelrepair::StepReport sr;
                    sr.name        = "Normal Map Displacement";
                    sr.was_run     = true;
                    sr.issues_fixed = dr.faces_after - dr.faces_before;
                    sr.duration_ms = dr.duration_ms;
                    report.steps.push_back(sr);
                    ++done;
                } catch (const std::exception& e) {
                    emit finished(std::move(mesh_), {},
                                  QString("Normal map displacement failed: ") + e.what());
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
            modelrepair::DecimateParams dp;
            dp.backend          = decimate_backend_;
            dp.ratio            = decimate_ratio_;
            dp.target_error     = decimate_target_error_;
            dp.normal_deviation = decimate_normal_dev_;
            auto dr = modelrepair::decimate(mesh_, dp);
            modelrepair::StepReport sr;
            sr.name         = "Decimate";
            sr.was_run      = true;
            sr.issues_fixed = static_cast<int>(dr.faces_before) - static_cast<int>(dr.faces_after);
            sr.duration_ms  = dr.duration_ms;
            report.steps.push_back(sr);
        } catch (const std::exception& e) {
            emit finished(std::move(mesh_), {}, QString("Decimation failed: ") + e.what());
            return;
        }
    }

    emit finished(std::move(mesh_), report, {});
}

} // namespace gui

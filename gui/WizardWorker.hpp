#pragma once

#include "modelrepair/Decimate.hpp"
#include "modelrepair/Mesh.hpp"
#include "modelrepair/RepairOptions.hpp"
#include "modelrepair/RepairReport.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

namespace gui
{

class WizardWorker : public QObject
{
    Q_OBJECT
public:
    enum class Phase { Repair, RemeshSmooth, Decimate };

    // Phase 1 — runs pipeline steps 1-6 only (no post-processing)
    explicit WizardWorker(modelrepair::Mesh mesh, modelrepair::RepairOptions opts,
                          QObject* parent = nullptr);

    // Phase 2 — remesh and/or smooth and/or subdivide
    explicit WizardWorker(modelrepair::Mesh mesh,
                          bool do_remesh, double remesh_factor, unsigned int remesh_iters,
                          bool do_smooth, unsigned int smooth_iters, double crease_angle,
                          bool use_vulkan,
                          bool do_subdivide, int subdivide_method, unsigned int subdivide_iters,
                          QObject* parent = nullptr);

    // Phase 3 — decimate
    explicit WizardWorker(modelrepair::Mesh mesh, double decimate_ratio,
                          modelrepair::DecimateBackend backend,
                          double target_error, double normal_deviation,
                          QObject* parent = nullptr);

public slots:
    void run();

signals:
    void progressChanged(int step, int total, const QString& name);
    void finished(modelrepair::Mesh result, modelrepair::RepairReport report, QString error);

private:
    Phase                      phase_;
    modelrepair::Mesh          mesh_;
    modelrepair::RepairOptions opts_;
    bool         do_remesh_        = false;
    double       remesh_factor_    = 0.8;
    unsigned int remesh_iters_     = 3;
    bool         do_smooth_        = false;
    unsigned int smooth_iters_     = 3;
    double       crease_angle_     = 45.0;
    bool         use_vulkan_       = false;
    bool         do_subdivide_     = false;
    int          subdivide_method_ = 0;   // 0=Loop, 1=CatmullClark
    unsigned int subdivide_iters_  = 1;
    double                       decimate_ratio_   = 0.5;
    modelrepair::DecimateBackend decimate_backend_ = modelrepair::DecimateBackend::CGAL;
    double                       decimate_target_error_ = 0.01;
    double                       decimate_normal_dev_   = 15.0;

    std::shared_ptr<std::atomic<bool>> cancel_flag_;

public:
    void set_cancel_flag(std::shared_ptr<std::atomic<bool>> f) { cancel_flag_ = std::move(f); }
};

} // namespace gui

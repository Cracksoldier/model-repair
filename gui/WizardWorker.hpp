#pragma once

#include "modelrepair/Mesh.hpp"
#include "modelrepair/RepairOptions.hpp"
#include "modelrepair/RepairReport.hpp"

#include <QObject>
#include <QString>

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

    // Phase 2 — remesh and/or smooth
    explicit WizardWorker(modelrepair::Mesh mesh,
                          bool do_remesh, double remesh_factor, unsigned int remesh_iters,
                          bool do_smooth, unsigned int smooth_iters, double crease_angle,
                          QObject* parent = nullptr);

    // Phase 3 — decimate
    explicit WizardWorker(modelrepair::Mesh mesh, double decimate_ratio,
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
    bool         do_remesh_      = false;
    double       remesh_factor_  = 0.8;
    unsigned int remesh_iters_   = 3;
    bool         do_smooth_      = false;
    unsigned int smooth_iters_   = 3;
    double       crease_angle_   = 45.0;
    double       decimate_ratio_ = 0.5;
};

} // namespace gui

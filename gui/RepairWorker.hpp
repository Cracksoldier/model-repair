#pragma once

#include "modelrepair/RepairOptions.hpp"
#include "modelrepair/RepairReport.hpp"
#include "modelrepair/Mesh.hpp"

#include <QObject>
#include <QString>
#include <atomic>
#include <filesystem>
#include <memory>

namespace gui
{

class RepairWorker : public QObject
{
    Q_OBJECT

public:
    explicit RepairWorker(std::filesystem::path input,
                          modelrepair::RepairOptions opts,
                          std::shared_ptr<std::atomic<bool>> cancel_flag,
                          QObject* parent = nullptr);

public slots:
    void run();

signals:
    void progressChanged(int step, int total, const QString& step_name);
    void finished(modelrepair::RepairReport report,
                  modelrepair::Mesh before_mesh,
                  modelrepair::Mesh after_mesh,
                  QString error);
    void cancelled(modelrepair::Mesh partial_mesh, int pipeline_steps_completed);

private:
    std::filesystem::path                   input_;
    modelrepair::RepairOptions              opts_;
    std::shared_ptr<std::atomic<bool>>      cancel_flag_;
};

} // namespace gui

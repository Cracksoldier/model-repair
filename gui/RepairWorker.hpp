#pragma once

#include "modelrepair/RepairOptions.hpp"
#include "modelrepair/RepairReport.hpp"
#include "modelrepair/Mesh.hpp"

#include <QObject>
#include <QString>
#include <filesystem>

namespace gui
{

class RepairWorker : public QObject
{
    Q_OBJECT

public:
    explicit RepairWorker(std::filesystem::path input,
                          modelrepair::RepairOptions opts,
                          QObject* parent = nullptr);

public slots:
    void run();

signals:
    void progressChanged(int step, int total, const QString& step_name);
    void finished(modelrepair::RepairReport report,
                  modelrepair::Mesh mesh,
                  QString error);

private:
    std::filesystem::path        input_;
    modelrepair::RepairOptions   opts_;
};

} // namespace gui

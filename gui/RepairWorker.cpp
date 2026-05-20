#include "RepairWorker.hpp"

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

    emit finished(report, std::move(before_mesh), std::move(mesh), {});
}

} // namespace gui

#include "MainWindow.hpp"
#include "PreviewWindow.hpp"
#include "RepairWorker.hpp"
#include "ReportView.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "modelrepair/io/MeshIO.hpp"

namespace gui
{

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("Model Repair");
    setMinimumSize(600, 640);
    setAcceptDrops(true);

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setSpacing(8);
    root->setContentsMargins(12, 12, 12, 12);

    // --- Drop zone ---
    drop_label_ = new QLabel("Drop an STL / OBJ / 3MF file here\nor click Open");
    drop_label_->setAlignment(Qt::AlignCenter);
    drop_label_->setStyleSheet(
        "border: 2px dashed #888; border-radius: 8px; padding: 24px; color: #555;");
    drop_label_->setMinimumHeight(80);
    root->addWidget(drop_label_);

    auto* btn_open = new QPushButton("Open file…");
    connect(btn_open, &QPushButton::clicked, this, &MainWindow::on_open_clicked);
    root->addWidget(btn_open);

    // --- Options ---
    auto* grp = new QGroupBox("Repair options");
    auto* grp_layout = new QVBoxLayout(grp);

    auto make_check = [&](const QString& label, bool checked = true) -> QCheckBox*
    {
        auto* cb = new QCheckBox(label);
        cb->setChecked(checked);
        grp_layout->addWidget(cb);
        return cb;
    };

    chk_merge_verts_    = make_check("Merge duplicate vertices");
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("  Merge tolerance:");
        spin_merge_tol_ = new QDoubleSpinBox;
        spin_merge_tol_->setDecimals(8);
        spin_merge_tol_->setSingleStep(1e-7);
        spin_merge_tol_->setValue(1e-6);
        spin_merge_tol_->setRange(0.0, 1.0);
        row->addWidget(lbl);
        row->addWidget(spin_merge_tol_);
        row->addStretch();
        grp_layout->addLayout(row);
    }
    chk_remove_degen_   = make_check("Remove degenerate triangles");
    chk_fix_nonmanifold_= make_check("Fix non-manifold geometry");
    chk_fix_normals_    = make_check("Fix face normals");
    chk_fill_holes_     = make_check("Fill holes");
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("  Max hole edges (0 = all):");
        spin_max_hole_edges_ = new QSpinBox;
        spin_max_hole_edges_->setRange(0, 100000);
        spin_max_hole_edges_->setValue(0);
        row->addWidget(lbl);
        row->addWidget(spin_max_hole_edges_);
        row->addStretch();
        grp_layout->addLayout(row);
    }
    chk_smooth_fill_    = make_check("  Smooth fill (uncheck = flat fan)");
    chk_self_intersect_ = make_check("Remove self-intersections (slow)");

    root->addWidget(grp);

    // --- Actions ---
    auto* btn_row = new QHBoxLayout;
    btn_repair_ = new QPushButton("Repair");
    btn_repair_->setEnabled(false);
    btn_repair_->setDefault(true);
    btn_save_   = new QPushButton("Save As…");
    btn_save_->setEnabled(false);
    btn_row->addWidget(btn_repair_);
    btn_row->addWidget(btn_save_);
    root->addLayout(btn_row);

    connect(btn_repair_, &QPushButton::clicked, this, &MainWindow::on_repair_clicked);
    connect(btn_save_,   &QPushButton::clicked, this, &MainWindow::on_save_clicked);

    // --- Progress ---
    progress_bar_ = new QProgressBar;
    progress_bar_->setRange(0, 6);
    progress_bar_->setTextVisible(true);
    progress_bar_->setValue(0);
    root->addWidget(progress_bar_);

    status_label_ = new QLabel;
    status_label_->setAlignment(Qt::AlignLeft);
    root->addWidget(status_label_);

    // --- Report ---
    report_view_ = new ReportView;
    root->addWidget(report_view_, 1);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;
    set_input(std::filesystem::path(urls.first().toLocalFile().toStdString()));
}

void MainWindow::on_open_clicked()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Open mesh", {},
        "Mesh files (*.stl *.obj *.3mf);;All files (*)");
    if (!path.isEmpty())
        set_input(std::filesystem::path(path.toStdString()));
}

void MainWindow::set_input(const std::filesystem::path& path)
{
    input_path_   = path;
    before_mesh_.reset();
    repaired_mesh_.reset();
    btn_repair_->setEnabled(true);
    btn_save_->setEnabled(false);
    report_view_->clear();
    drop_label_->setText(QString::fromStdString(path.filename().string()));
    status_label_->clear();
    progress_bar_->setValue(0);
}

modelrepair::RepairOptions MainWindow::collect_options() const
{
    modelrepair::RepairOptions o;
    o.merge_duplicate_vertices    = chk_merge_verts_->isChecked();
    o.merge_tolerance             = spin_merge_tol_->value();
    o.remove_degenerate_triangles = chk_remove_degen_->isChecked();
    o.fix_non_manifold            = chk_fix_nonmanifold_->isChecked();
    o.fix_normals                 = chk_fix_normals_->isChecked();
    o.fill_holes                  = chk_fill_holes_->isChecked();
    o.max_hole_edges              = static_cast<std::size_t>(spin_max_hole_edges_->value());
    o.fill_holes_smooth           = chk_smooth_fill_->isChecked();
    o.remove_self_intersections   = chk_self_intersect_->isChecked();
    return o;
}

void MainWindow::on_repair_clicked()
{
    if (!input_path_)
        return;

    set_busy(true);
    progress_bar_->setValue(0);
    status_label_->setText("Loading…");
    report_view_->clear();

    auto* worker = new RepairWorker(*input_path_, collect_options());
    worker_thread_ = new QThread(this);
    worker->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::started,  worker, &RepairWorker::run);
    connect(worker, &RepairWorker::progressChanged, this, &MainWindow::on_progress);
    connect(worker, &RepairWorker::finished,        this, &MainWindow::on_repair_finished);
    connect(worker, &RepairWorker::finished, worker_thread_, &QThread::quit);
    connect(worker_thread_, &QThread::finished, worker,        &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);

    worker_thread_->start();
}

void MainWindow::on_progress(int step, int total, const QString& name)
{
    progress_bar_->setMaximum(total);
    progress_bar_->setValue(step);
    status_label_->setText(name);
}

void MainWindow::on_repair_finished(modelrepair::RepairReport report,
                                    modelrepair::Mesh before_mesh,
                                    modelrepair::Mesh after_mesh,
                                    QString error)
{
    set_busy(false);
    worker_thread_ = nullptr;

    if (!error.isEmpty())
    {
        QMessageBox::critical(this, "Repair failed", error);
        status_label_->setText("Error: " + error);
        return;
    }

    before_mesh_   = std::move(before_mesh);
    repaired_mesh_ = std::move(after_mesh);
    report_view_->set_report(report);
    progress_bar_->setValue(progress_bar_->maximum());
    status_label_->setText(
        QString("Done — %1 manifold, %2 closed")
            .arg(report.is_manifold_after ? "✓" : "✗")
            .arg(report.is_closed_after   ? "✓" : "✗"));
    btn_save_->setEnabled(true);

    auto* preview = new PreviewWindow(*before_mesh_, *repaired_mesh_, this);
    preview->setAttribute(Qt::WA_DeleteOnClose);
    preview->show();
}

void MainWindow::on_save_clicked()
{
    if (!repaired_mesh_)
        return;

    QString suggestion;
    if (input_path_)
    {
        auto stem = input_path_->stem().string() + "_repaired";
        suggestion = QString::fromStdString(
            (input_path_->parent_path() / (stem + input_path_->extension().string())).string());
    }

    QString path = QFileDialog::getSaveFileName(
        this, "Save repaired mesh", suggestion,
        "STL binary (*.stl);;OBJ (*.obj);;3MF (*.3mf);;All files (*)");
    if (path.isEmpty())
        return;

    try
    {
        modelrepair::io::save(*repaired_mesh_, std::filesystem::path(path.toStdString()));
        status_label_->setText("Saved: " + path);
    }
    catch (const std::exception& e)
    {
        QMessageBox::critical(this, "Save failed", e.what());
    }
}

void MainWindow::set_busy(bool busy)
{
    btn_repair_->setEnabled(!busy);
    btn_save_->setEnabled(!busy && repaired_mesh_.has_value());
}

} // namespace gui

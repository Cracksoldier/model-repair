#include "MainWindow.hpp"
#include "BatchWindow.hpp"
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
#include <QTimer>
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
    drop_label_ = new QLabel("Drop an STL / OBJ / 3MF / GLB / glTF file here\nor click Open");
    drop_label_->setAlignment(Qt::AlignCenter);
    drop_label_->setStyleSheet(
        "border: 2px dashed #888; border-radius: 8px; padding: 24px; color: #555;");
    drop_label_->setMinimumHeight(80);
    root->addWidget(drop_label_);

    btn_open_ = new QPushButton("Open file…");
    connect(btn_open_, &QPushButton::clicked, this, &MainWindow::on_open_clicked);
    root->addWidget(btn_open_);

    // --- Options ---
    opts_group_ = new QGroupBox("Repair options");
    auto* grp = opts_group_;
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
    chk_self_intersect_ = make_check("Remove self-intersections (very slow — avoid on large meshes)", false);

    // Remeshing
    chk_remesh_ = make_check("Remesh (isotropic) before smoothing [experimental]", false);
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("  Edge length factor:");
        spin_remesh_factor_ = new QDoubleSpinBox;
        spin_remesh_factor_->setRange(0.1, 2.0);
        spin_remesh_factor_->setSingleStep(0.1);
        spin_remesh_factor_->setDecimals(2);
        spin_remesh_factor_->setValue(0.8);
        spin_remesh_factor_->setEnabled(false);
        row->addWidget(lbl);
        row->addWidget(spin_remesh_factor_);
        row->addStretch();
        grp_layout->addLayout(row);
        connect(chk_remesh_, &QCheckBox::toggled,
                spin_remesh_factor_, &QDoubleSpinBox::setEnabled);
    }
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("  Iterations:");
        spn_remesh_iters_ = new QSpinBox;
        spn_remesh_iters_->setRange(1, 10);
        spn_remesh_iters_->setValue(3);
        spn_remesh_iters_->setEnabled(false);
        row->addWidget(lbl);
        row->addWidget(spn_remesh_iters_);
        row->addStretch();
        grp_layout->addLayout(row);
        connect(chk_remesh_, &QCheckBox::toggled,
                spn_remesh_iters_, &QSpinBox::setEnabled);
    }

    // Smoothing
    chk_smooth_ = make_check("Smooth after repair [experimental]", false);
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("  Iterations:");
        spn_smooth_iters_ = new QSpinBox;
        spn_smooth_iters_->setRange(1, 50);
        spn_smooth_iters_->setValue(3);
        spn_smooth_iters_->setEnabled(false);
        row->addWidget(lbl);
        row->addWidget(spn_smooth_iters_);
        row->addStretch();
        grp_layout->addLayout(row);
        connect(chk_smooth_, &QCheckBox::toggled,
                spn_smooth_iters_, &QSpinBox::setEnabled);
    }
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("  Crease angle (°):");
        spn_smooth_crease_ = new QDoubleSpinBox;
        spn_smooth_crease_->setRange(0.0, 180.0);
        spn_smooth_crease_->setSingleStep(5.0);
        spn_smooth_crease_->setDecimals(1);
        spn_smooth_crease_->setValue(45.0);
        spn_smooth_crease_->setEnabled(false);
        row->addWidget(lbl);
        row->addWidget(spn_smooth_crease_);
        row->addStretch();
        grp_layout->addLayout(row);
        connect(chk_smooth_, &QCheckBox::toggled,
                spn_smooth_crease_, &QDoubleSpinBox::setEnabled);
    }

    // Decimation
    chk_decimate_ = make_check("Decimate after repair", false);
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("  Retain fraction:");
        spin_decimate_ratio_ = new QDoubleSpinBox;
        spin_decimate_ratio_->setRange(0.01, 1.0);
        spin_decimate_ratio_->setSingleStep(0.05);
        spin_decimate_ratio_->setDecimals(2);
        spin_decimate_ratio_->setValue(0.5);
        spin_decimate_ratio_->setEnabled(false);
        row->addWidget(lbl);
        row->addWidget(spin_decimate_ratio_);
        row->addStretch();
        grp_layout->addLayout(row);
        connect(chk_decimate_, &QCheckBox::toggled,
                spin_decimate_ratio_, &QDoubleSpinBox::setEnabled);
    }

    root->addWidget(grp);

    // --- Actions ---
    auto* btn_row = new QHBoxLayout;
    btn_repair_   = new QPushButton("Repair");
    btn_repair_->setEnabled(false);
    btn_repair_->setDefault(true);
    btn_diagnose_ = new QPushButton("Diagnose");
    btn_diagnose_->setEnabled(false);
    btn_save_     = new QPushButton("Save As…");
    btn_save_->setEnabled(false);
    btn_row->addWidget(btn_repair_);
    btn_row->addWidget(btn_diagnose_);
    btn_row->addWidget(btn_save_);
    btn_batch_ = new QPushButton("Batch Repair…");
    btn_row->addWidget(btn_batch_);
    root->addLayout(btn_row);

    connect(btn_repair_,   &QPushButton::clicked, this, &MainWindow::on_repair_clicked);
    connect(btn_diagnose_, &QPushButton::clicked, this, &MainWindow::on_diagnose_clicked);
    connect(btn_save_,     &QPushButton::clicked, this, &MainWindow::on_save_clicked);
    connect(btn_batch_,    &QPushButton::clicked, this, [this]
    {
        auto* batch = new BatchWindow(collect_options(), this);
        batch->setAttribute(Qt::WA_DeleteOnClose);
        batch->show();
    });

    // --- Progress ---
    progress_bar_ = new QProgressBar;
    progress_bar_->setRange(0, 6);
    progress_bar_->setTextVisible(true);
    progress_bar_->setValue(0);
    root->addWidget(progress_bar_);

    auto* status_row = new QHBoxLayout;
    status_label_ = new QLabel;
    status_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    elapsed_label_ = new QLabel;
    elapsed_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_row->addWidget(status_label_, 1);
    status_row->addWidget(elapsed_label_);
    root->addLayout(status_row);

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
    btn_diagnose_->setEnabled(true);
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
    o.remesh                    = chk_remesh_->isChecked();
    o.remesh_edge_length_factor = spin_remesh_factor_->value();
    o.remesh_iterations         = static_cast<unsigned int>(spn_remesh_iters_->value());
    o.smooth             = chk_smooth_->isChecked();
    o.smooth_iterations  = static_cast<unsigned int>(spn_smooth_iters_->value());
    o.smooth_crease_angle = spn_smooth_crease_->value();
    o.decimate                    = chk_decimate_->isChecked();
    o.decimate_ratio              = spin_decimate_ratio_->value();
    return o;
}

void MainWindow::on_repair_clicked()
{
    if (!input_path_)
        return;
    start_worker(collect_options());
}

void MainWindow::on_diagnose_clicked()
{
    if (!input_path_)
        return;
    auto opts = collect_options();
    opts.diagnose_only = true;
    opts.smooth        = false;
    opts.decimate      = false;
    start_worker(opts);
}

void MainWindow::start_worker(modelrepair::RepairOptions opts)
{
    set_busy(true);
    progress_bar_->setValue(0);
    status_label_->setText("Loading…");
    report_view_->clear();

    auto* worker = new RepairWorker(*input_path_, std::move(opts));
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
    progress_bar_->setValue(step - 1); // step N started = N-1 steps done; 100% set on finish
    status_label_->setText(name);
    task_clock_.restart();
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

    if (report.diagnose_only)
    {
        status_label_->setText("Diagnosis complete — no changes made");
        repaired_mesh_.reset();
        btn_save_->setEnabled(false);
    }
    else
    {
        status_label_->setText(
            QString("Done — %1 manifold, %2 closed")
                .arg(report.is_manifold_after ? "✓" : "✗")
                .arg(report.is_closed_after   ? "✓" : "✗"));
        btn_save_->setEnabled(true);

        auto* preview = new PreviewWindow(*before_mesh_, *repaired_mesh_, this);
        preview->setAttribute(Qt::WA_DeleteOnClose);
        preview->show();
    }
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
    btn_diagnose_->setEnabled(!busy && input_path_.has_value());
    btn_save_->setEnabled(!busy && repaired_mesh_.has_value());

    btn_open_->setEnabled(!busy);
    btn_batch_->setEnabled(!busy);
    drop_label_->setEnabled(!busy);
    opts_group_->setEnabled(!busy);

    if (busy) {
        elapsed_clock_.start();
        task_clock_.start();
        elapsed_label_->setText("0:00 / 0:00");
        if (!elapsed_timer_) {
            elapsed_timer_ = new QTimer(this);
            elapsed_timer_->setInterval(1000);
            connect(elapsed_timer_, &QTimer::timeout,
                    this, &MainWindow::on_elapsed_tick);
        }
        elapsed_timer_->start();
    } else {
        if (elapsed_timer_) elapsed_timer_->stop();
    }
}

void MainWindow::on_elapsed_tick()
{
    auto fmt = [](qint64 ms) -> QString {
        qint64 s = ms / 1000;
        return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    };
    elapsed_label_->setText(fmt(elapsed_clock_.elapsed()) + " / " + fmt(task_clock_.elapsed()));
}

} // namespace gui

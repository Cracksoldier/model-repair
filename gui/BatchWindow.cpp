#include "BatchWindow.hpp"
#include "PreviewWindow.hpp"
#include "RepairWorker.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include "modelrepair/io/MeshIO.hpp"

namespace gui
{

namespace
{
    enum Col { ColFilename = 0, ColStatus, ColIssues, ColWatertight, ColTime, ColCount };
}

BatchWindow::BatchWindow(modelrepair::RepairOptions initial_opts, QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Batch Repair");
    setMinimumSize(720, 600);
    setAcceptDrops(true);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(12, 12, 12, 12);

    // --- Repair options ---
    auto* grp        = new QGroupBox("Repair options");
    auto* grp_layout = new QVBoxLayout(grp);

    auto make_check = [&](const QString& label, bool checked) -> QCheckBox*
    {
        auto* cb = new QCheckBox(label);
        cb->setChecked(checked);
        grp_layout->addWidget(cb);
        return cb;
    };

    chk_merge_verts_     = make_check("Merge duplicate vertices",          initial_opts.merge_duplicate_vertices);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("  Merge tolerance:"));
        spin_merge_tol_ = new QDoubleSpinBox;
        spin_merge_tol_->setDecimals(8);
        spin_merge_tol_->setSingleStep(1e-7);
        spin_merge_tol_->setValue(initial_opts.merge_tolerance);
        spin_merge_tol_->setRange(0.0, 1.0);
        row->addWidget(spin_merge_tol_);
        row->addStretch();
        grp_layout->addLayout(row);
    }
    chk_remove_degen_    = make_check("Remove degenerate triangles",        initial_opts.remove_degenerate_triangles);
    chk_fix_nonmanifold_ = make_check("Fix non-manifold geometry",          initial_opts.fix_non_manifold);
    chk_fix_normals_     = make_check("Fix face normals",                   initial_opts.fix_normals);
    chk_fill_holes_      = make_check("Fill holes",                         initial_opts.fill_holes);
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("  Max hole edges (0 = all):"));
        spin_max_hole_edges_ = new QSpinBox;
        spin_max_hole_edges_->setRange(0, 100000);
        spin_max_hole_edges_->setValue(static_cast<int>(initial_opts.max_hole_edges));
        row->addWidget(spin_max_hole_edges_);
        row->addStretch();
        grp_layout->addLayout(row);
    }
    chk_smooth_fill_     = make_check("  Smooth fill (uncheck = flat fan)", initial_opts.fill_holes_smooth);
    chk_self_intersect_  = make_check("Remove self-intersections (slow)",   initial_opts.remove_self_intersections);
    root->addWidget(grp);

    // --- Output directory ---
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("Output dir:"));
        lbl_output_dir_ = new QLabel("(same as input, _repaired suffix)");
        lbl_output_dir_->setStyleSheet("color: #666;");
        row->addWidget(lbl_output_dir_, 1);
        auto* btn_browse = new QPushButton("Browse…");
        connect(btn_browse, &QPushButton::clicked, this, &BatchWindow::on_browse_output_clicked);
        row->addWidget(btn_browse);
        root->addLayout(row);
    }

    // --- File table ---
    table_ = new QTableWidget(0, ColCount);
    table_->setHorizontalHeaderLabels({"Filename", "Status", "Issues (f/f)", "Watertight", "Time (ms)"});
    table_->horizontalHeader()->setSectionResizeMode(ColFilename,   QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColStatus,     QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColIssues,     QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColWatertight, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColTime,       QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &BatchWindow::on_selection_changed);
    root->addWidget(table_, 1);

    // --- File action buttons ---
    {
        auto* row = new QHBoxLayout;
        btn_add_     = new QPushButton("Add Files…");
        btn_remove_  = new QPushButton("Remove");
        btn_preview_ = new QPushButton("Preview Selected");
        btn_remove_->setEnabled(false);
        btn_preview_->setEnabled(false);
        row->addWidget(btn_add_);
        row->addWidget(btn_remove_);
        row->addStretch();
        row->addWidget(btn_preview_);
        root->addLayout(row);
    }
    connect(btn_add_,     &QPushButton::clicked, this, &BatchWindow::on_add_files_clicked);
    connect(btn_remove_,  &QPushButton::clicked, this, &BatchWindow::on_remove_clicked);
    connect(btn_preview_, &QPushButton::clicked, this, &BatchWindow::on_preview_clicked);

    // --- Overall progress + start/cancel ---
    {
        auto* row = new QHBoxLayout;
        overall_progress_ = new QProgressBar;
        overall_progress_->setRange(0, 1);
        overall_progress_->setValue(0);
        overall_progress_->setTextVisible(false);
        row->addWidget(overall_progress_, 1);
        btn_start_cancel_ = new QPushButton("Start Repair");
        btn_start_cancel_->setEnabled(false);
        row->addWidget(btn_start_cancel_);
        root->addLayout(row);
    }
    connect(btn_start_cancel_, &QPushButton::clicked, this, &BatchWindow::on_start_cancel_clicked);

    lbl_status_ = new QLabel;
    lbl_status_->setAlignment(Qt::AlignLeft);
    root->addWidget(lbl_status_);
}

void BatchWindow::closeEvent(QCloseEvent* event)
{
    if (current_job_ != -1)
        event->ignore(); // block close while repair is running; click Cancel first
    else
        event->accept();
}

void BatchWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BatchWindow::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const auto& url : event->mimeData()->urls())
        paths << url.toLocalFile();
    if (!paths.isEmpty())
        add_files(paths);
}

void BatchWindow::add_files(const QStringList& paths)
{
    for (const auto& path_str : paths)
    {
        std::filesystem::path p(path_str.toStdString());
        bool already = false;
        for (const auto& j : jobs_)
            if (j.input_path == p) { already = true; break; }
        if (already)
            continue;

        jobs_.push_back({p, {}, {}, {}, {}});
        int row = table_->rowCount();
        table_->insertRow(row);

        auto* fn_item = new QTableWidgetItem(QString::fromStdString(p.filename().string()));
        fn_item->setToolTip(path_str);
        table_->setItem(row, ColFilename,   fn_item);

        auto* st_item = new QTableWidgetItem("Pending");
        st_item->setForeground(Qt::gray);
        table_->setItem(row, ColStatus,     st_item);
        table_->setItem(row, ColIssues,     new QTableWidgetItem(""));
        table_->setItem(row, ColWatertight, new QTableWidgetItem(""));
        table_->setItem(row, ColTime,       new QTableWidgetItem(""));
    }
    btn_start_cancel_->setEnabled(!jobs_.empty());
}

void BatchWindow::on_add_files_clicked()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "Add mesh files", {},
        "Mesh files (*.stl *.obj *.3mf);;All files (*)");
    if (!paths.isEmpty())
        add_files(paths);
}

void BatchWindow::on_remove_clicked()
{
    if (current_job_ != -1)
        return;
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(jobs_.size()))
        return;
    table_->removeRow(row);
    jobs_.erase(jobs_.begin() + row);
    btn_start_cancel_->setEnabled(!jobs_.empty());
}

void BatchWindow::on_browse_output_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select output directory",
        output_dir_ ? QString::fromStdString(output_dir_->string()) : QString{});
    if (dir.isEmpty())
        return;
    output_dir_ = std::filesystem::path(dir.toStdString());
    lbl_output_dir_->setText(dir);
    lbl_output_dir_->setStyleSheet("");
}

void BatchWindow::on_preview_clicked()
{
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(jobs_.size()))
        return;
    const auto& job = jobs_[row];
    if (!job.before_mesh || !job.after_mesh)
        return;
    auto* preview = new PreviewWindow(*job.before_mesh, *job.after_mesh, this);
    preview->setAttribute(Qt::WA_DeleteOnClose);
    preview->show();
}

void BatchWindow::on_selection_changed()
{
    int row = table_->currentRow();
    bool can_preview = row >= 0 &&
                       row < static_cast<int>(jobs_.size()) &&
                       jobs_[row].before_mesh.has_value() &&
                       jobs_[row].after_mesh.has_value();
    btn_preview_->setEnabled(can_preview);

    bool can_remove = row >= 0 && current_job_ == -1;
    btn_remove_->setEnabled(can_remove);
}

void BatchWindow::on_start_cancel_clicked()
{
    if (current_job_ != -1)
    {
        cancel_requested_ = true;
        btn_start_cancel_->setText("Cancelling…");
        btn_start_cancel_->setEnabled(false);
        return;
    }

    // Find first unprocessed job
    int first = -1;
    for (int i = 0; i < static_cast<int>(jobs_.size()); ++i)
    {
        if (!jobs_[i].report && jobs_[i].error.isEmpty())
        {
            first = i;
            break;
        }
    }
    if (first == -1)
        return;

    current_job_ = first;
    btn_start_cancel_->setText("Cancel");
    btn_add_->setEnabled(false);
    btn_remove_->setEnabled(false);

    int total    = static_cast<int>(jobs_.size());
    int done_cnt = 0;
    for (const auto& j : jobs_)
        if (j.report.has_value() || !j.error.isEmpty()) done_cnt++;

    overall_progress_->setFormat("%v / %m");
    overall_progress_->setTextVisible(true);
    overall_progress_->setRange(0, total);
    overall_progress_->setValue(done_cnt);

    start_next_job();
}

void BatchWindow::start_next_job()
{
    if (cancel_requested_)
    {
        for (int i = current_job_; i < static_cast<int>(jobs_.size()); ++i)
        {
            if (!jobs_[i].report && jobs_[i].error.isEmpty())
            {
                table_->item(i, ColStatus)->setText("Cancelled");
                table_->item(i, ColStatus)->setForeground(Qt::gray);
            }
        }
        cancel_requested_ = false;
        current_job_      = -1;
        btn_start_cancel_->setText("Start Repair");
        btn_start_cancel_->setEnabled(true);
        btn_add_->setEnabled(true);
        on_selection_changed();
        lbl_status_->setText("Cancelled");
        return;
    }

    // Skip over already-processed slots (safety guard)
    while (current_job_ < static_cast<int>(jobs_.size()) &&
           (jobs_[current_job_].report.has_value() || !jobs_[current_job_].error.isEmpty()))
    {
        current_job_++;
    }

    if (current_job_ >= static_cast<int>(jobs_.size()))
    {
        current_job_ = -1;
        btn_start_cancel_->setText("Start Repair");
        btn_start_cancel_->setEnabled(!jobs_.empty());
        btn_add_->setEnabled(true);
        on_selection_changed();

        int done = 0, errors = 0;
        for (const auto& j : jobs_)
        {
            if (j.report.has_value()) done++;
            if (!j.error.isEmpty())  errors++;
        }
        lbl_status_->setText(QString("Done — %1 repaired, %2 error(s)").arg(done).arg(errors));
        return;
    }

    int row = current_job_;
    table_->item(row, ColStatus)->setText("Loading…");
    table_->item(row, ColStatus)->setForeground(Qt::blue);
    table_->scrollToItem(table_->item(row, ColStatus));
    lbl_status_->setText(QString("File %1 / %2: loading…")
        .arg(row + 1).arg(jobs_.size()));

    auto* worker   = new RepairWorker(jobs_[row].input_path, collect_options());
    worker_thread_ = new QThread(this);
    worker->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::started,          worker,        &RepairWorker::run);
    connect(worker,  &RepairWorker::progressChanged,    this,          &BatchWindow::on_progress);
    connect(worker,  &RepairWorker::finished,           this,          &BatchWindow::on_file_finished);
    connect(worker,  &RepairWorker::finished,           worker_thread_, &QThread::quit);
    connect(worker_thread_, &QThread::finished,         worker,        &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished,         worker_thread_, &QObject::deleteLater);

    worker_thread_->start();
}

void BatchWindow::on_progress(int /*step*/, int /*total*/, const QString& name)
{
    if (current_job_ < 0 || current_job_ >= static_cast<int>(jobs_.size()))
        return;
    table_->item(current_job_, ColStatus)->setText(name + "…");
    lbl_status_->setText(QString("File %1 / %2: %3")
        .arg(current_job_ + 1).arg(jobs_.size()).arg(name));
}

void BatchWindow::on_file_finished(modelrepair::RepairReport report,
                                   modelrepair::Mesh before_mesh,
                                   modelrepair::Mesh after_mesh,
                                   QString error)
{
    worker_thread_ = nullptr;
    int row = current_job_;

    if (!error.isEmpty())
    {
        jobs_[row].error = error;
        table_->item(row, ColStatus)->setText("✗ " + error);
        table_->item(row, ColStatus)->setForeground(Qt::red);
    }
    else
    {
        jobs_[row].report      = std::move(report);
        jobs_[row].before_mesh = std::move(before_mesh);
        jobs_[row].after_mesh  = std::move(after_mesh);

        // Auto-save
        try
        {
            modelrepair::io::save(*jobs_[row].after_mesh, output_path_for(jobs_[row]));
            table_->item(row, ColStatus)->setText("✓ Done");
            table_->item(row, ColStatus)->setForeground(QColor(42, 122, 42));
        }
        catch (const std::exception& e)
        {
            jobs_[row].error = QString("Save failed: ") + e.what();
            table_->item(row, ColStatus)->setText("✗ " + jobs_[row].error);
            table_->item(row, ColStatus)->setForeground(Qt::red);
        }

        // Summarise issues and timing across all steps
        std::size_t total_found = 0, total_fixed = 0;
        double      total_ms    = 0.0;
        for (const auto& step : jobs_[row].report->steps)
        {
            if (!step.was_run) continue;
            total_found += step.issues_found;
            total_fixed += step.issues_fixed;
            total_ms    += step.duration_ms;
        }

        auto* issues_item = new QTableWidgetItem(
            QString("%1 / %2").arg(total_found).arg(total_fixed));
        if (total_found > 0 && total_fixed < total_found)
            issues_item->setForeground(Qt::darkYellow);
        table_->setItem(row, ColIssues, issues_item);

        table_->setItem(row, ColWatertight, new QTableWidgetItem(
            jobs_[row].report->is_closed_after && jobs_[row].report->is_manifold_after
                ? "✓" : "✗"));
        table_->setItem(row, ColTime, new QTableWidgetItem(
            QString::number(total_ms, 'f', 0)));
    }

    overall_progress_->setValue(row + 1);
    on_selection_changed();

    current_job_++;
    start_next_job();
}

modelrepair::RepairOptions BatchWindow::collect_options() const
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

std::filesystem::path BatchWindow::output_path_for(const Job& job) const
{
    if (output_dir_)
        return *output_dir_ / job.input_path.filename();
    auto stem = job.input_path.stem().string() + "_repaired";
    return job.input_path.parent_path() / (stem + job.input_path.extension().string());
}

} // namespace gui

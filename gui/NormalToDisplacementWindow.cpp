#include "NormalToDisplacementWindow.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QException>
#include <QResizeEvent>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>

namespace gui
{

NormalToDisplacementWindow::NormalToDisplacementWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle("Normal Map → Displacement Map");
    setMinimumSize(720, 480);

    auto* root = new QHBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(12, 12, 12, 12);

    // ── Left panel: input + settings + actions ───────────────────────────────
    auto* left = new QVBoxLayout;
    left->setSpacing(6);
    root->addLayout(left, 0);

    // File picker row
    {
        auto* row = new QHBoxLayout;
        edit_path_ = new QLineEdit;
        edit_path_->setPlaceholderText("Normal map path…");
        edit_path_->setMinimumWidth(220);
        auto* btn_browse = new QPushButton("Browse…");
        row->addWidget(edit_path_);
        row->addWidget(btn_browse);
        left->addLayout(row);
        connect(btn_browse, &QPushButton::clicked,
                this, &NormalToDisplacementWindow::on_browse_clicked);
    }

    // Helper: add a checkbox
    auto add_check = [&](const QString& label, bool val) -> QCheckBox*
    {
        auto* cb = new QCheckBox(label);
        cb->setChecked(val);
        left->addWidget(cb);
        return cb;
    };

    // Helper: add a double spinbox row
    auto add_dspin = [&](const QString& label,
                         double mn, double mx, double val,
                         double step, int dec) -> QDoubleSpinBox*
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label));
        auto* sb = new QDoubleSpinBox;
        sb->setRange(mn, mx);
        sb->setValue(val);
        sb->setSingleStep(step);
        sb->setDecimals(dec);
        row->addWidget(sb);
        row->addStretch();
        left->addLayout(row);
        return sb;
    };

    // Helper: add an int spinbox row
    auto add_ispin = [&](const QString& label, int mn, int mx, int val) -> QSpinBox*
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label));
        auto* sb = new QSpinBox;
        sb->setRange(mn, mx);
        sb->setValue(val);
        row->addWidget(sb);
        row->addStretch();
        left->addLayout(row);
        return sb;
    };

    chk_flip_green_ = add_check("Flip green channel (DirectX normal maps)", false);
    chk_invert_     = add_check("Invert height", false);
    chk_normalize_  = add_check("Normalize output", true);
    spin_gradient_  = add_dspin("Gradient strength:", 0.01, 20.0, 1.0, 0.1, 2);
    spin_contrast_  = add_dspin("Contrast:", 0.1, 10.0, 1.0, 0.1, 2);
    spin_blur_      = add_dspin("Blur radius (px):", 0.0, 100.0, 0.0, 1.0, 0);
    spin_iters_     = add_ispin("Solver iterations:", 50, 5000, 1000);

    left->addStretch();

    // Action buttons
    {
        auto* row = new QHBoxLayout;
        btn_run_    = new QPushButton("Run");
        btn_export_ = new QPushButton("Export 16-bit PNG…");
        btn_export_->setEnabled(false);
        row->addWidget(btn_run_);
        row->addWidget(btn_export_);
        left->addLayout(row);
    }

    progress_bar_ = new QProgressBar;
    progress_bar_->setRange(0, 0);   // indeterminate
    progress_bar_->setVisible(false);
    left->addWidget(progress_bar_);

    lbl_status_ = new QLabel;
    lbl_status_->setWordWrap(true);
    lbl_status_->setStyleSheet("color: #aaa; font-size: 11px;");
    left->addWidget(lbl_status_);

    // ── Right panel: preview ─────────────────────────────────────────────────
    lbl_preview_ = new QLabel("(run to preview)");
    lbl_preview_->setAlignment(Qt::AlignCenter);
    lbl_preview_->setMinimumSize(300, 300);
    lbl_preview_->setStyleSheet("border: 1px solid #555; background: #111; color: #666;");
    lbl_preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(lbl_preview_, 1);

    // ── Async watcher ────────────────────────────────────────────────────────
    watcher_ = new QFutureWatcher<modelrepair::NormalToDisplacementResult>(this);
    connect(watcher_,
            &QFutureWatcher<modelrepair::NormalToDisplacementResult>::finished,
            this,
            &NormalToDisplacementWindow::on_result_ready);

    connect(btn_run_,    &QPushButton::clicked,
            this, &NormalToDisplacementWindow::on_run_clicked);
    connect(btn_export_, &QPushButton::clicked,
            this, &NormalToDisplacementWindow::on_export_clicked);
}

// ── Event overrides ──────────────────────────────────────────────────────────

void NormalToDisplacementWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    update_preview();
}

// ── Private helpers ──────────────────────────────────────────────────────────

modelrepair::NormalToDisplacementSettings
NormalToDisplacementWindow::collect_settings() const
{
    modelrepair::NormalToDisplacementSettings s;
    s.flip_green        = chk_flip_green_->isChecked();
    s.invert_height     = chk_invert_->isChecked();
    s.normalize_output  = chk_normalize_->isChecked();
    s.gradient_strength = static_cast<float>(spin_gradient_->value());
    s.contrast          = static_cast<float>(spin_contrast_->value());
    s.blur_radius       = static_cast<float>(spin_blur_->value());
    s.solver_max_iter   = spin_iters_->value();
    return s;
}

void NormalToDisplacementWindow::set_running(bool running)
{
    btn_run_->setEnabled(!running);
    btn_export_->setEnabled(!running && !result_.height.empty());
    progress_bar_->setVisible(running);
}

void NormalToDisplacementWindow::update_preview()
{
    if (result_.height.empty()) return;

    const int W = result_.width;
    const int H = result_.height_px;

    QImage preview(W, H, QImage::Format_Grayscale8);
    for (int y = 0; y < H; ++y)
    {
        uchar* row = preview.scanLine(y);
        for (int x = 0; x < W; ++x)
        {
            const float v = std::clamp(
                result_.height[static_cast<std::size_t>(y * W + x)], 0.f, 1.f);
            row[x] = static_cast<uchar>(v * 255.f + 0.5f);
        }
    }

    const QPixmap pix = QPixmap::fromImage(preview).scaled(
        lbl_preview_->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    lbl_preview_->setPixmap(pix);
}

// ── Slots ────────────────────────────────────────────────────────────────────

void NormalToDisplacementWindow::on_browse_clicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Open normal map", {},
        "Images (*.png *.jpg *.jpeg *.tga *.bmp);;All files (*)");
    if (!path.isEmpty())
        edit_path_->setText(path);
}

void NormalToDisplacementWindow::on_run_clicked()
{
    const QString path = edit_path_->text().trimmed();
    if (path.isEmpty())
    {
        QMessageBox::warning(this, "No input", "Please select a normal map file first.");
        return;
    }

    set_running(true);
    lbl_status_->setText("Solving Poisson system…");

    const std::string path_str = path.toStdString();
    const modelrepair::NormalToDisplacementSettings s = collect_settings();

    watcher_->setFuture(QtConcurrent::run(
        [path_str, s]() -> modelrepair::NormalToDisplacementResult
        {
            return modelrepair::normal_to_displacement(path_str, s);
        }));
}

void NormalToDisplacementWindow::on_result_ready()
{
    try
    {
        result_ = watcher_->result();
    }
    catch (const QUnhandledException& e)
    {
        // QtConcurrent wraps non-QException throws in QUnhandledException.
        // Rethrow the stored exception_ptr to recover the original message.
        set_running(false);
        QString msg = "Unknown error during conversion.";
        if (e.exception()) {
            try { std::rethrow_exception(e.exception()); }
            catch (const std::exception& inner)
                { msg = QString("Error: %1").arg(inner.what()); }
            catch (...) {}
        }
        lbl_status_->setText(msg);
        return;
    }
    catch (const std::exception& e)
    {
        set_running(false);
        lbl_status_->setText(QString("Error: %1").arg(e.what()));
        return;
    }
    catch (...)
    {
        set_running(false);
        lbl_status_->setText("Unknown error during conversion.");
        return;
    }

    set_running(false);   // result_ is now populated; Export enabled via set_running
    lbl_status_->setText(
        QString("%1\xC3\x97%2  |  %3 ms")
            .arg(result_.width)
            .arg(result_.height_px)
            .arg(static_cast<double>(result_.duration_ms), 0, 'f', 0));
    update_preview();
}

void NormalToDisplacementWindow::on_export_clicked()
{
    if (result_.height.empty()) return;

    const QString path = QFileDialog::getSaveFileName(
        this, "Export displacement map", {},
        "16-bit PNG (*.png);;All files (*)");
    if (path.isEmpty()) return;

    const int W = result_.width;
    const int H = result_.height_px;

    QImage img(W, H, QImage::Format_Grayscale16);
    for (int y = 0; y < H; ++y)
    {
        auto* row = reinterpret_cast<quint16*>(img.scanLine(y));
        for (int x = 0; x < W; ++x)
        {
            const float v = std::clamp(
                result_.height[static_cast<std::size_t>(y * W + x)], 0.f, 1.f);
            row[x] = static_cast<quint16>(v * 65535.f + 0.5f);
        }
    }

    if (!img.save(path))
        QMessageBox::critical(this, "Export failed", "Could not save " + path);
    else
        lbl_status_->setText("Exported: " + path);
}

} // namespace gui

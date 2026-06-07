#pragma once

#include "modelrepair/NormalToDisplacement.hpp"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QResizeEvent>
#include <QWidget>

class QTimer;

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace gui
{

class NormalToDisplacementWindow : public QWidget
{
    Q_OBJECT

public:
    explicit NormalToDisplacementWindow(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void on_browse_clicked();
    void on_run_clicked();
    void on_cancel_clicked();
    void on_export_clicked();
    void on_result_ready();
    void on_progress(int iter);
    void on_elapsed_tick();

private:
    void set_running(bool running);
    void update_preview();
    modelrepair::NormalToDisplacementSettings collect_settings() const;

    // Input
    QLineEdit*    edit_path_;
    QPushButton*  btn_browse_;

    // Settings
    QCheckBox*      chk_flip_green_;
    QCheckBox*      chk_invert_;
    QCheckBox*      chk_normalize_;
    QCheckBox*      chk_use_vulkan_ = nullptr;  // null when Vulkan is unavailable
    QDoubleSpinBox* spin_gradient_;
    QDoubleSpinBox* spin_contrast_;
    QDoubleSpinBox* spin_blur_;
    QSpinBox*       spin_iters_;

    // Actions
    QPushButton*  btn_run_;
    QPushButton*  btn_cancel_;
    QPushButton*  btn_export_;
    QProgressBar* progress_bar_;
    QLabel*       lbl_status_;
    QLabel*       lbl_elapsed_;

    // Timer state
    QElapsedTimer elapsed_clock_;
    QTimer*       elapsed_timer_ = nullptr;

    // Preview
    QLabel* lbl_preview_;

    // Result
    modelrepair::NormalToDisplacementResult result_;
    QFutureWatcher<modelrepair::NormalToDisplacementResult>* watcher_;
};

} // namespace gui

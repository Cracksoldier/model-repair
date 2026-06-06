#pragma once

#include "modelrepair/NormalToDisplacement.hpp"

#include <QFutureWatcher>
#include <QWidget>

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

private slots:
    void on_browse_clicked();
    void on_run_clicked();
    void on_export_clicked();
    void on_result_ready();

private:
    void set_running(bool running);
    void update_preview();
    modelrepair::NormalToDisplacementSettings collect_settings() const;

    // Input
    QLineEdit*      edit_path_;

    // Settings
    QCheckBox*      chk_flip_green_;
    QCheckBox*      chk_invert_;
    QCheckBox*      chk_normalize_;
    QDoubleSpinBox* spin_gradient_;
    QDoubleSpinBox* spin_contrast_;
    QDoubleSpinBox* spin_blur_;
    QSpinBox*       spin_iters_;

    // Actions
    QPushButton*  btn_run_;
    QPushButton*  btn_export_;
    QProgressBar* progress_bar_;
    QLabel*       lbl_status_;

    // Preview
    QLabel* lbl_preview_;

    // Result
    modelrepair::NormalToDisplacementResult result_;
    QFutureWatcher<modelrepair::NormalToDisplacementResult>* watcher_;
};

} // namespace gui

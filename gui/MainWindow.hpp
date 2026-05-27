#pragma once

#include "modelrepair/RepairOptions.hpp"
#include "modelrepair/RepairReport.hpp"
#include "modelrepair/Mesh.hpp"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>

class QLabel;
class QPushButton;
class QProgressBar;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QGroupBox;
class QThread;
class QTimer;

namespace gui
{

class ReportView;
class WizardWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void on_open_clicked();
    void on_repair_clicked();
    void on_diagnose_clicked();
    void on_cancel_clicked();
    void on_save_clicked();
    void on_progress(int step, int total, const QString& name);
    void on_repair_finished(modelrepair::RepairReport report,
                            modelrepair::Mesh before_mesh,
                            modelrepair::Mesh after_mesh,
                            QString error);
    void on_repair_cancelled(modelrepair::Mesh partial_mesh, int pipeline_steps_completed);
    void on_elapsed_tick();

private:
    void set_input(const std::filesystem::path& path);
    void set_busy(bool busy);
    modelrepair::RepairOptions collect_options() const;
    void start_worker(modelrepair::RepairOptions opts);

    // Input / status
    QLabel*      drop_label_;
    QLabel*      status_label_;
    QLabel*      elapsed_label_;
    QProgressBar* progress_bar_;

    // Option widgets
    QGroupBox*    opts_group_;
    QCheckBox*    chk_merge_verts_;
    QDoubleSpinBox* spin_merge_tol_;
    QCheckBox*    chk_remove_degen_;
    QCheckBox*    chk_fix_nonmanifold_;
    QCheckBox*    chk_fix_normals_;
    QCheckBox*    chk_fill_holes_;
    QSpinBox*     spin_max_hole_edges_;
    QCheckBox*    chk_smooth_fill_;
    QCheckBox*    chk_self_intersect_;

    // Remesh option widgets
    QCheckBox*      chk_remesh_;
    QDoubleSpinBox* spin_remesh_factor_;
    QSpinBox*       spn_remesh_iters_;

    // Smoothing option widgets
    QCheckBox*      chk_smooth_;
    QSpinBox*       spn_smooth_iters_;
    QDoubleSpinBox* spn_smooth_crease_;

    // Decimation option widgets
    QCheckBox*      chk_decimate_;
    QDoubleSpinBox* spin_decimate_ratio_;

    // Actions
    QPushButton* btn_repair_;
    QPushButton* btn_diagnose_;
    QPushButton* btn_cancel_;
    QPushButton* btn_save_;
    QPushButton* btn_open_;
    QPushButton* btn_batch_;
    QPushButton* btn_wizard_;

    // Results
    ReportView* report_view_;

    // State
    std::optional<std::filesystem::path> input_path_;
    std::optional<modelrepair::Mesh>     before_mesh_;
    std::optional<modelrepair::Mesh>     repaired_mesh_;

    QThread*      worker_thread_  = nullptr;
    QTimer*       elapsed_timer_  = nullptr;
    QElapsedTimer elapsed_clock_;
    QElapsedTimer task_clock_;
    WizardWindow* wizard_         = nullptr;
    std::shared_ptr<std::atomic<bool>> cancel_flag_;
};

} // namespace gui

#pragma once

#include "modelrepair/RepairOptions.hpp"
#include "modelrepair/RepairReport.hpp"
#include "modelrepair/Mesh.hpp"

#include <QWidget>
#include <filesystem>
#include <optional>
#include <vector>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QThread;

namespace gui
{

class BatchWindow : public QWidget
{
    Q_OBJECT

public:
    explicit BatchWindow(modelrepair::RepairOptions initial_opts, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void on_add_files_clicked();
    void on_remove_clicked();
    void on_start_cancel_clicked();
    void on_browse_output_clicked();
    void on_preview_clicked();
    void on_selection_changed();
    void on_progress(int step, int total, const QString& name);
    void on_file_finished(modelrepair::RepairReport report,
                          modelrepair::Mesh before_mesh,
                          modelrepair::Mesh after_mesh,
                          QString error);

private:
    struct Job
    {
        std::filesystem::path                    input_path;
        std::optional<modelrepair::RepairReport> report;
        std::optional<modelrepair::Mesh>         before_mesh;
        std::optional<modelrepair::Mesh>         after_mesh;
        QString                                  error;
    };

    void add_files(const QStringList& paths);
    void start_next_job();
    modelrepair::RepairOptions collect_options() const;
    std::filesystem::path output_path_for(const Job& job) const;

    // Option widgets
    QCheckBox*      chk_merge_verts_;
    QDoubleSpinBox* spin_merge_tol_;
    QCheckBox*      chk_remove_degen_;
    QCheckBox*      chk_fix_nonmanifold_;
    QCheckBox*      chk_fix_normals_;
    QCheckBox*      chk_fill_holes_;
    QSpinBox*       spin_max_hole_edges_;
    QCheckBox*      chk_smooth_fill_;
    QCheckBox*      chk_self_intersect_;

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
    QComboBox*      combo_decimate_backend_;
    QLabel*         lbl_decimate_info_;
    QWidget*        decimate_extra_params_;
    QDoubleSpinBox* spin_decimate_target_error_;
    QDoubleSpinBox* spin_decimate_normal_dev_;

    // Output directory
    QLabel*      lbl_output_dir_;
    std::optional<std::filesystem::path> output_dir_;

    // File table
    QTableWidget* table_;

    // Buttons / progress
    QPushButton*  btn_add_;
    QPushButton*  btn_remove_;
    QPushButton*  btn_preview_;
    QPushButton*  btn_start_cancel_;
    QProgressBar* overall_progress_;
    QLabel*       lbl_status_;

    // State
    std::vector<Job> jobs_;
    int  current_job_      = -1;
    bool cancel_requested_ = false;
    QThread* worker_thread_ = nullptr;
};

} // namespace gui

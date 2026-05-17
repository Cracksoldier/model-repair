#pragma once

#include "modelrepair/RepairOptions.hpp"
#include "modelrepair/RepairReport.hpp"
#include "modelrepair/Mesh.hpp"

#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <filesystem>
#include <optional>

class QLabel;
class QPushButton;
class QProgressBar;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QGroupBox;
class QThread;

namespace gui
{

class ReportView;

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
    void on_save_clicked();
    void on_progress(int step, int total, const QString& name);
    void on_repair_finished(modelrepair::RepairReport report,
                            modelrepair::Mesh mesh,
                            QString error);

private:
    void set_input(const std::filesystem::path& path);
    void set_busy(bool busy);
    modelrepair::RepairOptions collect_options() const;

    // Input / status
    QLabel*      drop_label_;
    QLabel*      status_label_;
    QProgressBar* progress_bar_;

    // Option widgets
    QCheckBox*    chk_merge_verts_;
    QDoubleSpinBox* spin_merge_tol_;
    QCheckBox*    chk_remove_degen_;
    QCheckBox*    chk_fix_nonmanifold_;
    QCheckBox*    chk_fix_normals_;
    QCheckBox*    chk_fill_holes_;
    QSpinBox*     spin_max_hole_edges_;
    QCheckBox*    chk_smooth_fill_;
    QCheckBox*    chk_self_intersect_;

    // Actions
    QPushButton* btn_repair_;
    QPushButton* btn_save_;

    // Results
    ReportView* report_view_;

    // State
    std::optional<std::filesystem::path> input_path_;
    std::optional<modelrepair::Mesh>     repaired_mesh_;

    QThread* worker_thread_ = nullptr;
};

} // namespace gui

#pragma once

#include "modelrepair/Mesh.hpp"
#include "modelrepair/RepairReport.hpp"

#include <QDialog>
#include <QElapsedTimer>
#include <filesystem>

#include <atomic>
#include <memory>

class QCloseEvent;
class QLabel;
class QPushButton;
class QStackedWidget;
class QThread;
class QTimer;

namespace gui
{

class Phase1Page;
class Phase2Page;
class Phase3Page;

class WizardWindow : public QDialog
{
    Q_OBJECT
public:
    explicit WizardWindow(std::filesystem::path input_path, QWidget* parent = nullptr);
    ~WizardWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void on_progress(int step, int total, const QString& name);
    void on_worker_finished(modelrepair::Mesh result, modelrepair::RepairReport report,
                            QString error);
    void on_cancel_clicked();
    void on_elapsed_tick();
    // Navigation from phase pages
    void on_phase1_run();
    void on_phase1_continue();
    void on_phase1_finish();
    void on_phase2_run();
    void on_phase2_retry();
    void on_phase2_skip();
    void on_phase2_continue();
    void on_phase2_finish();
    void on_phase3_run(double ratio);
    void on_phase3_retry();
    void on_phase3_skip();
    void on_phase3_save();

private:
    void start_worker_thread(class WizardWorker* worker);
    void try_save_and_accept();
    void enter_phase2();
    void enter_phase3();

    std::filesystem::path input_path_;
    modelrepair::Mesh     current_mesh_;
    modelrepair::Mesh     phase_start_mesh_;

    QLabel*         phase_header_ = nullptr;
    QPushButton*    btn_cancel_   = nullptr;
    QStackedWidget* pages_        = nullptr;
    Phase1Page*     page1_        = nullptr;
    Phase2Page*     page2_        = nullptr;
    Phase3Page*     page3_        = nullptr;

    QThread* worker_thread_ = nullptr;
    int      current_phase_ = 1;    // 1, 2, or 3

    std::shared_ptr<std::atomic<bool>> cancel_flag_;

    QTimer*       elapsed_timer_ = nullptr;
    QElapsedTimer elapsed_clock_;
    QElapsedTimer step_clock_;
};

} // namespace gui

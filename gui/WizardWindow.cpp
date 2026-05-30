#include "WizardWindow.hpp"
#include "timer_util.hpp"
#include "WizardWorker.hpp"
#include "MeshViewWidget.hpp"

#include "modelrepair/RepairOptions.hpp"
#include "modelrepair/Smooth.hpp"
#include "modelrepair/io/MeshIO.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <QCloseEvent>

#include <memory>
#include <optional>

namespace gui
{

// ─── helpers ──────────────────────────────────────────────────────────────────

static QLabel* info_label(const QString& text, QWidget* parent = nullptr)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setWordWrap(true);
    lbl->setStyleSheet("color: #666; font-size: 11px; margin-left: 22px;");
    return lbl;
}

static QLabel* warn_label(const QString& text, QWidget* parent = nullptr)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setWordWrap(true);
    lbl->setStyleSheet("color: #b05000; font-weight: bold; margin-left: 22px;");
    return lbl;
}

// Builds the standard before/after 3D view and inserts it into container.
// Existing child widgets and layout are replaced on each call.
static void populate_preview_area(QWidget* container,
                                   const modelrepair::Mesh& before,
                                   const modelrepair::Mesh& after)
{
    // Delete the old layout first (which owns sub-layouts like left/right QVBoxLayout).
    // Do this before deleting child widgets so QLayout doesn't access freed widget pointers.
    delete container->layout();

    // Delete any child widgets left alive (MeshViewWidget, QLabel).
    // These are direct children of container so findChildren is safe.
    const auto old_children =
        container->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* c : old_children)
        delete c;

    auto camera = std::make_shared<CameraState>();
    auto* view_before = new MeshViewWidget(before, camera, container);
    auto* view_after  = new MeshViewWidget(after,  camera, container);
    view_before->set_peer(view_after);
    view_after->set_peer(view_before);

    auto* lbl_b = new QLabel("Before", container);
    auto* lbl_a = new QLabel("After",  container);
    lbl_b->setAlignment(Qt::AlignCenter);
    lbl_a->setAlignment(Qt::AlignCenter);

    auto* left  = new QVBoxLayout;
    auto* right = new QVBoxLayout;
    left ->addWidget(lbl_b);
    left ->addWidget(view_before, 1);
    right->addWidget(lbl_a);
    right->addWidget(view_after,  1);

    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    row->addLayout(left);
    row->addLayout(right);
}

static QString face_stat(const modelrepair::Mesh& before, const modelrepair::Mesh& after)
{
    const auto bf = before.num_faces();
    const auto af = after.num_faces();
    const int delta = static_cast<int>(af) - static_cast<int>(bf);
    const QString sign = delta >= 0 ? "+" : "";
    return QString("Faces: %1 → %2  (%3%4)").arg(bf).arg(af).arg(sign).arg(delta);
}

static QString decimate_time_hint(std::size_t faces)
{
    if (faces < 50'000)
        return "Expected time: under 5 seconds.";
    if (faces < 200'000)
        return "Expected time: 5 – 30 seconds.";
    if (faces < 500'000)
        return "⚠ Expected time: 30 seconds – 2 minutes. This mesh is large.";
    return "⚠ Expected time: several minutes. This mesh is very large — consider decimating first.";
}

// ─── Phase1Page ───────────────────────────────────────────────────────────────

class Phase1Page : public QWidget
{
    Q_OBJECT
public:
    explicit Phase1Page(QWidget* parent = nullptr) : QWidget(parent)
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);

        inner_ = new QStackedWidget(this);
        root->addWidget(inner_);

        inner_->addWidget(build_options());
        inner_->addWidget(build_running());
        inner_->addWidget(build_preview());
        inner_->setCurrentIndex(0);
    }

    modelrepair::RepairOptions collect_options() const
    {
        modelrepair::RepairOptions o;
        o.merge_duplicate_vertices    = chk_merge_->isChecked();
        o.remove_degenerate_triangles = chk_degen_->isChecked();
        o.fix_non_manifold            = chk_nonmani_->isChecked();
        o.fix_normals                 = chk_normals_->isChecked();
        o.fill_holes                  = chk_holes_->isChecked();
        o.remove_self_intersections   = chk_selfintersect_->isChecked();
        return o;
    }

    void show_options()  { inner_->setCurrentIndex(0); }
    void show_running()  { inner_->setCurrentIndex(1); }

    void update_progress(int step, int total, const QString& name)
    {
        progress_->setMaximum(total);
        progress_->setValue(step - 1);
        status_->setText(name);
    }

    void set_elapsed(const QString& text) { elapsed_label_->setText(text); }
    int steps_done()  const { return progress_->value(); }
    int steps_total() const { return progress_->maximum(); }

    void show_preview(modelrepair::Mesh before, modelrepair::Mesh after,
                      const modelrepair::RepairReport& report)
    {
        before_mesh_ = std::move(before);
        after_mesh_  = std::move(after);
        populate_preview_area(view_area_, *before_mesh_, *after_mesh_);
        stats_label_->setText(face_stat(*before_mesh_, *after_mesh_)
            + QString("   |  Manifold: %1   Closed: %2")
                .arg(report.is_manifold_after ? "✓" : "✗")
                .arg(report.is_closed_after   ? "✓" : "✗"));
        progress_->setValue(progress_->maximum());
        inner_->setCurrentIndex(2);
    }

signals:
    void run_clicked();
    void continue_clicked();
    void finish_clicked();

private:
    QWidget* build_options()
    {
        auto* w   = new QWidget;
        auto* sa  = new QScrollArea;
        sa->setWidget(w);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);

        auto* vbox = new QVBoxLayout(w);
        vbox->setSpacing(4);

        auto add_step = [&](QCheckBox*& chk, const QString& label, bool checked,
                            const QString& desc, const QString& warn = {})
        {
            chk = new QCheckBox(label);
            chk->setChecked(checked);
            vbox->addWidget(chk);
            vbox->addWidget(info_label(desc));
            if (!warn.isEmpty())
                vbox->addWidget(warn_label(warn));
        };

        add_step(chk_merge_,        "Merge duplicate vertices",     true,
            "Merges coincident points — required for STL files, which store each triangle with independent vertices.");
        add_step(chk_degen_,        "Remove degenerate triangles",  true,
            "Removes zero-area triangles that can cause failures in subsequent steps.");
        add_step(chk_nonmani_,      "Fix non-manifold geometry",    true,
            "Ensures every edge is shared by exactly two triangles — required for a watertight mesh.");
        add_step(chk_normals_,      "Fix face normals",             true,
            "Orients all triangles consistently outward.");
        add_step(chk_holes_,        "Fill holes",                   true,
            "Closes open boundaries by inserting new triangles.");
        add_step(chk_selfintersect_,"Remove self-intersections",    false,
            "Removes faces that overlap each other.",
            "⚠ Slow — can take several minutes on complex meshes. Uses an exact arithmetic kernel internally.");

        vbox->addStretch();

        auto* btn_row = new QHBoxLayout;
        auto* btn_run = new QPushButton("Run Repair →");
        btn_run->setDefault(true);
        connect(btn_run, &QPushButton::clicked, this, &Phase1Page::run_clicked);
        btn_row->addStretch();
        btn_row->addWidget(btn_run);

        auto* outer = new QVBoxLayout;
        outer->addWidget(sa, 1);
        outer->addLayout(btn_row);
        auto* wrapper = new QWidget;
        wrapper->setLayout(outer);
        return wrapper;
    }

    QWidget* build_running()
    {
        auto* w    = new QWidget;
        auto* vbox = new QVBoxLayout(w);
        vbox->addStretch();
        progress_ = new QProgressBar;
        progress_->setRange(0, 7);
        progress_->setValue(0);
        status_ = new QLabel;
        status_->setAlignment(Qt::AlignCenter);
        elapsed_label_ = new QLabel;
        elapsed_label_->setAlignment(Qt::AlignCenter);
        vbox->addWidget(progress_);
        vbox->addWidget(status_);
        vbox->addWidget(elapsed_label_);
        vbox->addStretch();
        return w;
    }

    QWidget* build_preview()
    {
        auto* w    = new QWidget;
        auto* vbox = new QVBoxLayout(w);

        view_area_ = new QWidget;
        vbox->addWidget(view_area_, 1);

        stats_label_ = new QLabel;
        stats_label_->setAlignment(Qt::AlignCenter);
        vbox->addWidget(stats_label_);

        auto* btn_row = new QHBoxLayout;
        auto* btn_finish   = new QPushButton("Save As…");
        auto* btn_continue = new QPushButton("Continue to Phase 2 →");
        btn_continue->setDefault(true);
        connect(btn_finish,   &QPushButton::clicked, this, &Phase1Page::finish_clicked);
        connect(btn_continue, &QPushButton::clicked, this, &Phase1Page::continue_clicked);
        btn_row->addWidget(btn_finish);
        btn_row->addStretch();
        btn_row->addWidget(btn_continue);
        vbox->addLayout(btn_row);
        return w;
    }

    QStackedWidget* inner_;
    QProgressBar*   progress_      = nullptr;
    QLabel*         status_        = nullptr;
    QLabel*         elapsed_label_ = nullptr;
    QWidget*        view_area_     = nullptr;
    QLabel*         stats_label_   = nullptr;

    QCheckBox* chk_merge_        = nullptr;
    QCheckBox* chk_degen_        = nullptr;
    QCheckBox* chk_nonmani_      = nullptr;
    QCheckBox* chk_normals_      = nullptr;
    QCheckBox* chk_holes_        = nullptr;
    QCheckBox* chk_selfintersect_= nullptr;

    std::optional<modelrepair::Mesh> before_mesh_;
    std::optional<modelrepair::Mesh> after_mesh_;
};

// ─── Phase2Page ───────────────────────────────────────────────────────────────

class Phase2Page : public QWidget
{
    Q_OBJECT
public:
    explicit Phase2Page(QWidget* parent = nullptr) : QWidget(parent)
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);

        inner_ = new QStackedWidget(this);
        root->addWidget(inner_);

        inner_->addWidget(build_options());
        inner_->addWidget(build_running());
        inner_->addWidget(build_preview());
        inner_->setCurrentIndex(0);
    }

    void set_face_count(std::size_t faces) { face_count_ = faces; update_warnings(); }

    struct Params {
        bool         do_remesh;
        double       remesh_factor;
        unsigned int remesh_iters;
        bool         do_smooth;
        unsigned int smooth_iters;
        double       crease_angle;
        bool         use_vulkan;
        bool         do_subdivide;
        int          subdivide_method;  // 0=Loop, 1=CatmullClark
        unsigned int subdivide_iters;
    };

    Params collect_params() const
    {
        return {
            chk_remesh_->isChecked(),
            static_cast<double>(spin_factor_->value()),
            static_cast<unsigned int>(spin_remesh_iters_->value()),
            chk_smooth_->isChecked(),
            static_cast<unsigned int>(spin_smooth_iters_->value()),
            static_cast<double>(spin_crease_->value()),
            chk_vulkan_ ? chk_vulkan_->isChecked() : false,
            chk_subdivide_->isChecked(),
            combo_subdiv_method_->currentIndex(),
            static_cast<unsigned int>(spin_subdiv_iters_->value()),
        };
    }

    void show_options() { inner_->setCurrentIndex(0); }

    void show_running()
    {
        total_steps_ = (chk_remesh_->isChecked()    ? spin_remesh_iters_->value() : 0)
                     + (chk_smooth_->isChecked()    ? spin_smooth_iters_->value() : 0)
                     + (chk_subdivide_->isChecked() ? 1 : 0);
        progress_->setRange(0, total_steps_);
        progress_->setValue(0);
        inner_->setCurrentIndex(1);
    }

    void update_progress(int step, int total, const QString& name)
    {
        progress_->setMaximum(total);
        progress_->setValue(step - 1);
        status_->setText(name);
    }

    void set_elapsed(const QString& text) { elapsed_label_->setText(text); }
    int steps_done()  const { return progress_->value(); }
    int steps_total() const { return progress_->maximum(); }

    void show_preview(modelrepair::Mesh before, modelrepair::Mesh after,
                      const modelrepair::RepairReport&)
    {
        before_mesh_ = std::move(before);
        after_mesh_  = std::move(after);
        populate_preview_area(view_area_, *before_mesh_, *after_mesh_);
        stats_label_->setText(face_stat(*before_mesh_, *after_mesh_));
        progress_->setValue(progress_->maximum());
        inner_->setCurrentIndex(2);
    }

signals:
    void run_clicked();
    void retry_clicked();
    void skip_clicked();
    void continue_clicked();
    void finish_clicked();

private:
    void update_warnings()
    {
        const bool remesh_on = chk_remesh_->isChecked();
        const bool fine = remesh_on && spin_factor_->value() < 0.61;
        warn_fine_->setVisible(fine);
        warn_large_->setVisible(remesh_on && !fine && face_count_ > 200'000);
        warn_smooth_->setVisible(chk_smooth_->isChecked() && spin_smooth_iters_->value() > 10);

        if (chk_subdivide_ && spin_subdiv_iters_ && warn_subdiv_) {
            const long long iters = spin_subdiv_iters_->value();
            long long factor = 1;
            for (int i = 0; i < iters; ++i) factor *= 4;
            warn_subdiv_->setVisible(
                chk_subdivide_->isChecked() && face_count_ * factor > 2'000'000);
        }
    }

    QWidget* build_options()
    {
        auto* w   = new QWidget;
        auto* sa  = new QScrollArea;
        sa->setWidget(w);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);

        auto* vbox = new QVBoxLayout(w);
        vbox->setSpacing(4);

        // ── Remesh ──────────────────────────────────────────────────────────
        chk_remesh_ = new QCheckBox("Isotropic Remesh [experimental]");
        chk_remesh_->setChecked(false);
        vbox->addWidget(chk_remesh_);
        vbox->addWidget(info_label(
            "Redistributes triangles for a more uniform size distribution. "
            "Most useful before smoothing on meshes with uneven triangle sizes (e.g. scanned or voxelised models)."));

        auto* remesh_body = new QWidget;
        remesh_body->setVisible(false);
        connect(chk_remesh_, &QCheckBox::toggled, remesh_body, &QWidget::setVisible);
        connect(chk_remesh_, &QCheckBox::toggled, this, [this](bool) { update_warnings(); });
        {
            auto* vb = new QVBoxLayout(remesh_body);
            vb->setContentsMargins(22, 0, 0, 0);
            vb->setSpacing(2);

            auto make_row = [&](const QString& lbl_text, QWidget* ctrl) -> QHBoxLayout*
            {
                auto* row = new QHBoxLayout;
                row->addWidget(new QLabel(lbl_text));
                row->addWidget(ctrl);
                row->addStretch();
                return row;
            };

            spin_factor_ = new QDoubleSpinBox;
            spin_factor_->setRange(0.1, 2.0);
            spin_factor_->setSingleStep(0.1);
            spin_factor_->setDecimals(2);
            spin_factor_->setValue(0.8);
            vb->addLayout(make_row("Edge length factor:", spin_factor_));
            vb->addWidget(info_label("< 1.0 = finer mesh (more triangles), > 1.0 = coarser mesh (fewer triangles). "
                                     "0.8 is a good starting point for organic shapes."));
            connect(spin_factor_, &QDoubleSpinBox::valueChanged,
                    this, [this](double) { update_warnings(); });

            spin_remesh_iters_ = new QSpinBox;
            spin_remesh_iters_->setRange(1, 10);
            spin_remesh_iters_->setValue(3);
            connect(spin_remesh_iters_, &QSpinBox::valueChanged, this, [this](int) { update_warnings(); });
            vb->addLayout(make_row("Iterations:", spin_remesh_iters_));
            vb->addWidget(info_label("More iterations produce a more uniform triangle distribution. "
                                     "Each iteration scales with mesh size."));

            warn_large_ = warn_label("⚠ This mesh is large — remeshing may take several minutes.");
            warn_large_->setVisible(false);
            vb->addWidget(warn_large_);

            warn_fine_ = warn_label(
                "⚠ Edge length factor ≤ 0.6 generates a very fine mesh — "
                "remeshing may take hours even on moderately sized models.");
            warn_fine_->setVisible(false);
            vb->addWidget(warn_fine_);

            vb->addWidget(info_label(
                "Note: remeshing runs on a single CPU core — CGAL's isotropic remeshing "
                "has no parallel implementation. Expect longer runtimes on meshes with "
                "more than ~100 K faces."));
        }
        vbox->addWidget(remesh_body);

        vbox->addSpacing(8);

        // ── Smooth ──────────────────────────────────────────────────────────
        chk_smooth_ = new QCheckBox("Feature-Preserving Smooth [experimental]");
        chk_smooth_->setChecked(false);
        vbox->addWidget(chk_smooth_);
        vbox->addWidget(info_label(
            "Repositions vertices to reduce surface roughness. "
            "Edges sharper than the crease angle are preserved as hard features."));

        auto* smooth_body = new QWidget;
        smooth_body->setVisible(false);
        connect(chk_smooth_, &QCheckBox::toggled, smooth_body, &QWidget::setVisible);
        connect(chk_smooth_, &QCheckBox::toggled, this, [this](bool) { update_warnings(); });
        {
            auto* vb = new QVBoxLayout(smooth_body);
            vb->setContentsMargins(22, 0, 0, 0);
            vb->setSpacing(2);

            auto make_row = [&](const QString& lbl_text, QWidget* ctrl) -> QHBoxLayout*
            {
                auto* row = new QHBoxLayout;
                row->addWidget(new QLabel(lbl_text));
                row->addWidget(ctrl);
                row->addStretch();
                return row;
            };

            spin_smooth_iters_ = new QSpinBox;
            spin_smooth_iters_->setRange(1, 50);
            spin_smooth_iters_->setValue(3);
            connect(spin_smooth_iters_, &QSpinBox::valueChanged, this, [this](int) { update_warnings(); });
            vb->addLayout(make_row("Iterations:", spin_smooth_iters_));
            vb->addWidget(info_label("More iterations produce a smoother surface. "
                                     "Beyond 10 iterations, gains become minimal."));

            warn_smooth_ = warn_label("⚠ More than 10 iterations gives diminishing returns "
                                      "and can over-smooth fine details.");
            warn_smooth_->setVisible(false);
            vb->addWidget(warn_smooth_);

            spin_crease_ = new QDoubleSpinBox;
            spin_crease_->setRange(0.0, 180.0);
            spin_crease_->setSingleStep(5.0);
            spin_crease_->setDecimals(1);
            spin_crease_->setValue(45.0);
            vb->addLayout(make_row("Crease angle (°):", spin_crease_));
            vb->addWidget(info_label("Edges with a dihedral angle above this value are treated as sharp features "
                                     "and protected from smoothing. 45° preserves most mechanical edges."));

            const bool vk_ok = modelrepair::smooth_vulkan_available();
            chk_vulkan_ = new QCheckBox("Use GPU (Vulkan)");
            chk_vulkan_->setChecked(false);
            if (!vk_ok)
                chk_vulkan_->setVisible(false);
            else
                chk_vulkan_->setEnabled(chk_smooth_->isChecked());
            connect(chk_smooth_, &QCheckBox::toggled,
                    chk_vulkan_, [this, vk_ok](bool on) {
                        chk_vulkan_->setEnabled(on && vk_ok);
                    });
            vb->addWidget(chk_vulkan_);
        }
        vbox->addWidget(smooth_body);

        vbox->addSpacing(8);

        // ── Subdivide ────────────────────────────────────────────────────────
        chk_subdivide_ = new QCheckBox("Subdivision (Loop / Catmull-Clark) [experimental]");
        chk_subdivide_->setChecked(false);
        vbox->addWidget(chk_subdivide_);
        vbox->addWidget(info_label(
            "Smoothly increases triangle count via subdivision. "
            "Use instead of remesh+smooth on low-poly or CAD-derived models."));

        auto* subdiv_body = new QWidget;
        subdiv_body->setVisible(false);
        connect(chk_subdivide_, &QCheckBox::toggled, subdiv_body, &QWidget::setVisible);
        connect(chk_subdivide_, &QCheckBox::toggled, this, [this](bool) { update_warnings(); });
        {
            auto* vb = new QVBoxLayout(subdiv_body);
            vb->setContentsMargins(22, 0, 0, 0);
            vb->setSpacing(2);

            auto make_row2 = [&](const QString& lbl_text, QWidget* ctrl) -> QHBoxLayout*
            {
                auto* row = new QHBoxLayout;
                row->addWidget(new QLabel(lbl_text));
                row->addWidget(ctrl);
                row->addStretch();
                return row;
            };

            combo_subdiv_method_ = new QComboBox;
            combo_subdiv_method_->addItem("Loop (triangles)");
            combo_subdiv_method_->addItem("Catmull-Clark (general)");
            vb->addLayout(make_row2("Method:", combo_subdiv_method_));
            vb->addWidget(info_label("Loop is ideal for pure triangle meshes. "
                                     "Catmull-Clark handles mixed/quad meshes."));

            spin_subdiv_iters_ = new QSpinBox;
            spin_subdiv_iters_->setRange(1, 4);
            spin_subdiv_iters_->setValue(1);
            connect(spin_subdiv_iters_, &QSpinBox::valueChanged,
                    this, [this](int) { update_warnings(); });
            vb->addLayout(make_row2("Iterations:", spin_subdiv_iters_));
            vb->addWidget(info_label("Face count grows ~4× per iteration. "
                                     "1–2 iterations is typical."));

            warn_subdiv_ = warn_label(
                "⚠ This many iterations will produce a very large mesh — "
                "expect long runtimes and high memory use.");
            warn_subdiv_->setVisible(false);
            vb->addWidget(warn_subdiv_);
        }
        vbox->addWidget(subdiv_body);

        vbox->addStretch();

        auto* btn_row = new QHBoxLayout;
        auto* btn_skip = new QPushButton("Skip this phase");
        auto* btn_run  = new QPushButton("Run →");
        btn_run->setDefault(true);
        connect(btn_skip, &QPushButton::clicked, this, &Phase2Page::skip_clicked);
        connect(btn_run,  &QPushButton::clicked, this, &Phase2Page::run_clicked);
        btn_row->addWidget(btn_skip);
        btn_row->addStretch();
        btn_row->addWidget(btn_run);

        auto* outer = new QVBoxLayout;
        outer->addWidget(sa, 1);
        outer->addLayout(btn_row);
        auto* wrapper = new QWidget;
        wrapper->setLayout(outer);
        return wrapper;
    }

    QWidget* build_running()
    {
        auto* w    = new QWidget;
        auto* vbox = new QVBoxLayout(w);
        vbox->addStretch();
        progress_ = new QProgressBar;
        status_   = new QLabel;
        status_->setAlignment(Qt::AlignCenter);
        elapsed_label_ = new QLabel;
        elapsed_label_->setAlignment(Qt::AlignCenter);
        vbox->addWidget(progress_);
        vbox->addWidget(status_);
        vbox->addWidget(elapsed_label_);
        vbox->addStretch();
        return w;
    }

    QWidget* build_preview()
    {
        auto* w    = new QWidget;
        auto* vbox = new QVBoxLayout(w);

        view_area_ = new QWidget;
        vbox->addWidget(view_area_, 1);

        stats_label_ = new QLabel;
        stats_label_->setAlignment(Qt::AlignCenter);
        vbox->addWidget(stats_label_);

        auto* btn_row = new QHBoxLayout;
        auto* btn_retry    = new QPushButton("← Adjust settings");
        auto* btn_finish   = new QPushButton("Save As…");
        auto* btn_continue = new QPushButton("Continue to Phase 3 →");
        btn_continue->setDefault(true);
        connect(btn_retry,    &QPushButton::clicked, this, &Phase2Page::retry_clicked);
        connect(btn_finish,   &QPushButton::clicked, this, &Phase2Page::finish_clicked);
        connect(btn_continue, &QPushButton::clicked, this, &Phase2Page::continue_clicked);
        btn_row->addWidget(btn_retry);
        btn_row->addWidget(btn_finish);
        btn_row->addStretch();
        btn_row->addWidget(btn_continue);
        vbox->addLayout(btn_row);
        return w;
    }

    QStackedWidget* inner_;
    QProgressBar*   progress_      = nullptr;
    QLabel*         status_        = nullptr;
    QLabel*         elapsed_label_ = nullptr;
    QWidget*        view_area_     = nullptr;
    QLabel*         stats_label_   = nullptr;

    QCheckBox*      chk_remesh_       = nullptr;
    QDoubleSpinBox* spin_factor_      = nullptr;
    QSpinBox*       spin_remesh_iters_= nullptr;
    QLabel*         warn_large_       = nullptr;
    QLabel*         warn_fine_        = nullptr;

    QCheckBox*      chk_smooth_       = nullptr;
    QSpinBox*       spin_smooth_iters_= nullptr;
    QDoubleSpinBox* spin_crease_      = nullptr;
    QLabel*         warn_smooth_      = nullptr;
    QCheckBox*      chk_vulkan_       = nullptr;

    QCheckBox* chk_subdivide_       = nullptr;
    QComboBox* combo_subdiv_method_ = nullptr;
    QSpinBox*  spin_subdiv_iters_   = nullptr;
    QLabel*    warn_subdiv_         = nullptr;

    std::size_t face_count_   = 0;
    int         total_steps_  = 1;

    std::optional<modelrepair::Mesh> before_mesh_;
    std::optional<modelrepair::Mesh> after_mesh_;
};

// ─── Phase3Page ───────────────────────────────────────────────────────────────

class Phase3Page : public QWidget
{
    Q_OBJECT
public:
    explicit Phase3Page(QWidget* parent = nullptr) : QWidget(parent)
    {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);

        inner_ = new QStackedWidget(this);
        root->addWidget(inner_);

        inner_->addWidget(build_options());
        inner_->addWidget(build_running());
        inner_->addWidget(build_preview());
        inner_->setCurrentIndex(0);
    }

    // Called when Phase 3 becomes active — populates analysis section
    void show_analysis(const modelrepair::Mesh& mesh)
    {
        const std::size_t faces = mesh.num_faces();
        const bool closed = mesh.is_closed();

        analysis_faces_->setText(QString("Triangles: <b>%1</b>").arg(faces));
        analysis_closed_->setText(QString("Mesh is: <b>%1</b>")
            .arg(closed ? "Watertight ✓" : "Open (not watertight) ✗"));
        analysis_time_->setText(decimate_time_hint(faces));
        analysis_open_warn_->setVisible(!closed);
        inner_->setCurrentIndex(0);
    }

    double decimate_ratio() const { return spin_ratio_->value(); }

    void show_running()
    {
        progress_->setRange(0, 1);
        progress_->setValue(0);
        status_->setText("Decimating…");
        inner_->setCurrentIndex(1);
    }

    void update_progress(int step, int total, const QString& name)
    {
        progress_->setMaximum(total);
        progress_->setValue(step - 1);
        status_->setText(name);
    }

    void set_elapsed(const QString& text) { elapsed_label_->setText(text); }
    int steps_done()  const { return progress_->value(); }
    int steps_total() const { return progress_->maximum(); }

    void show_preview(modelrepair::Mesh before, modelrepair::Mesh after,
                      const modelrepair::RepairReport&)
    {
        before_mesh_ = std::move(before);
        after_mesh_  = std::move(after);
        populate_preview_area(view_area_, *before_mesh_, *after_mesh_);
        const auto bf = before_mesh_->num_faces();
        const auto af = after_mesh_->num_faces();
        const double pct = bf > 0 ? 100.0 * af / bf : 0.0;
        stats_label_->setText(
            QString("Faces: %1 → %2  (%3% of original)").arg(bf).arg(af).arg(pct, 0, 'f', 1));
        progress_->setValue(progress_->maximum());
        inner_->setCurrentIndex(2);
    }

signals:
    void run_clicked(double ratio);
    void retry_clicked();
    void skip_clicked();
    void save_clicked();

private:
    QWidget* build_options()
    {
        auto* w   = new QWidget;
        auto* sa  = new QScrollArea;
        sa->setWidget(w);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);

        auto* vbox = new QVBoxLayout(w);
        vbox->setSpacing(4);

        // Analysis section
        auto* analysis_group = new QGroupBox("Mesh Analysis");
        auto* ag_layout = new QVBoxLayout(analysis_group);
        analysis_faces_  = new QLabel;
        analysis_closed_ = new QLabel;
        analysis_time_   = new QLabel;
        analysis_time_->setWordWrap(true);
        analysis_open_warn_ = warn_label(
            "ℹ Mesh is not watertight — decimation results may be less predictable.");
        analysis_open_warn_->setVisible(false);
        ag_layout->addWidget(analysis_faces_);
        ag_layout->addWidget(analysis_closed_);
        ag_layout->addWidget(analysis_time_);
        ag_layout->addWidget(analysis_open_warn_);
        vbox->addWidget(analysis_group);

        vbox->addSpacing(8);

        // Decimate options
        auto* dec_group = new QGroupBox("Decimate Options");
        auto* dg_layout = new QVBoxLayout(dec_group);
        dg_layout->addWidget(info_label(
            "Reduces the triangle count to improve rendering performance and file size, "
            "while preserving the overall shape as closely as possible."));

        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("Retain fraction:"));
        spin_ratio_ = new QDoubleSpinBox;
        spin_ratio_->setRange(0.01, 1.0);
        spin_ratio_->setSingleStep(0.05);
        spin_ratio_->setDecimals(2);
        spin_ratio_->setValue(0.5);
        row->addWidget(spin_ratio_);
        row->addStretch();
        dg_layout->addLayout(row);
        dg_layout->addWidget(info_label(
            "Fraction of triangles to keep. 0.5 = half, 0.25 = quarter. "
            "Lower values reduce detail; 0.7 – 0.8 is usually imperceptible."));
        vbox->addWidget(dec_group);
        vbox->addStretch();

        auto* btn_row = new QHBoxLayout;
        auto* btn_skip = new QPushButton("Skip this phase");
        auto* btn_run  = new QPushButton("Run Decimate →");
        btn_run->setDefault(true);
        connect(btn_skip, &QPushButton::clicked, this, &Phase3Page::skip_clicked);
        connect(btn_run,  &QPushButton::clicked, this, [this] {
            emit run_clicked(spin_ratio_->value());
        });
        btn_row->addWidget(btn_skip);
        btn_row->addStretch();
        btn_row->addWidget(btn_run);

        auto* outer = new QVBoxLayout;
        outer->addWidget(sa, 1);
        outer->addLayout(btn_row);
        auto* wrapper = new QWidget;
        wrapper->setLayout(outer);
        return wrapper;
    }

    QWidget* build_running()
    {
        auto* w    = new QWidget;
        auto* vbox = new QVBoxLayout(w);
        vbox->addStretch();
        progress_ = new QProgressBar;
        status_   = new QLabel;
        status_->setAlignment(Qt::AlignCenter);
        elapsed_label_ = new QLabel;
        elapsed_label_->setAlignment(Qt::AlignCenter);
        vbox->addWidget(progress_);
        vbox->addWidget(status_);
        vbox->addWidget(elapsed_label_);
        vbox->addStretch();
        return w;
    }

    QWidget* build_preview()
    {
        auto* w    = new QWidget;
        auto* vbox = new QVBoxLayout(w);

        view_area_ = new QWidget;
        vbox->addWidget(view_area_, 1);

        stats_label_ = new QLabel;
        stats_label_->setAlignment(Qt::AlignCenter);
        vbox->addWidget(stats_label_);

        auto* btn_row = new QHBoxLayout;
        auto* btn_retry = new QPushButton("← Adjust settings");
        auto* btn_close = new QPushButton("Close");
        auto* btn_save  = new QPushButton("Save As…");
        btn_save->setDefault(true);
        connect(btn_retry, &QPushButton::clicked, this, &Phase3Page::retry_clicked);
        connect(btn_close, &QPushButton::clicked, this, &Phase3Page::skip_clicked);
        connect(btn_save,  &QPushButton::clicked, this, &Phase3Page::save_clicked);
        btn_row->addWidget(btn_retry);
        btn_row->addWidget(btn_close);
        btn_row->addStretch();
        btn_row->addWidget(btn_save);
        vbox->addLayout(btn_row);
        return w;
    }

    QStackedWidget* inner_;
    QProgressBar*   progress_      = nullptr;
    QLabel*         status_        = nullptr;
    QLabel*         elapsed_label_ = nullptr;
    QWidget*        view_area_     = nullptr;
    QLabel*         stats_label_   = nullptr;

    QLabel*         analysis_faces_    = nullptr;
    QLabel*         analysis_closed_   = nullptr;
    QLabel*         analysis_time_     = nullptr;
    QLabel*         analysis_open_warn_= nullptr;
    QDoubleSpinBox* spin_ratio_        = nullptr;

    std::optional<modelrepair::Mesh> before_mesh_;
    std::optional<modelrepair::Mesh> after_mesh_;
};

// ─── WizardWindow ─────────────────────────────────────────────────────────────

WizardWindow::WizardWindow(std::filesystem::path input_path, QWidget* parent)
    : QDialog(parent), input_path_(std::move(input_path))
{
    setWindowTitle("Repair Wizard");
    resize(900, 680);

    try {
        current_mesh_ = modelrepair::io::load(input_path_);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Load failed", e.what());
        QMetaObject::invokeMethod(this, &QDialog::reject, Qt::QueuedConnection);
        return;
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // Phase indicator
    auto* phase_header = new QLabel;
    phase_header->setStyleSheet("font-size: 14px; font-weight: bold;");
    phase_header->setText("Phase 1 of 3 — Repair");
    root->addWidget(phase_header);
    phase_header_ = phase_header;

    // Separator
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    // Page stack
    pages_ = new QStackedWidget(this);
    page1_ = new Phase1Page(this);
    page2_ = new Phase2Page(this);
    page3_ = new Phase3Page(this);
    pages_->addWidget(page1_);
    pages_->addWidget(page2_);
    pages_->addWidget(page3_);
    pages_->setCurrentIndex(0);
    root->addWidget(pages_, 1);

    // Cancel button
    auto* cancel_row = new QHBoxLayout;
    btn_cancel_ = new QPushButton("Cancel");
    cancel_row->addStretch();
    cancel_row->addWidget(btn_cancel_);
    connect(btn_cancel_, &QPushButton::clicked, this, &WizardWindow::on_cancel_clicked);
    root->addLayout(cancel_row);

    // Wire page signals
    connect(page1_, &Phase1Page::run_clicked,      this, &WizardWindow::on_phase1_run);
    connect(page1_, &Phase1Page::continue_clicked,  this, &WizardWindow::on_phase1_continue);
    connect(page1_, &Phase1Page::finish_clicked,    this, &WizardWindow::on_phase1_finish);

    connect(page2_, &Phase2Page::run_clicked,      this, &WizardWindow::on_phase2_run);
    connect(page2_, &Phase2Page::retry_clicked,    this, &WizardWindow::on_phase2_retry);
    connect(page2_, &Phase2Page::skip_clicked,     this, &WizardWindow::on_phase2_skip);
    connect(page2_, &Phase2Page::continue_clicked, this, &WizardWindow::on_phase2_continue);
    connect(page2_, &Phase2Page::finish_clicked,   this, &WizardWindow::on_phase2_finish);

    connect(page3_, &Phase3Page::run_clicked,   this, &WizardWindow::on_phase3_run);
    connect(page3_, &Phase3Page::retry_clicked, this, &WizardWindow::on_phase3_retry);
    connect(page3_, &Phase3Page::skip_clicked,  this, &WizardWindow::on_phase3_skip);
    connect(page3_, &Phase3Page::save_clicked,  this, &WizardWindow::on_phase3_save);
}

WizardWindow::~WizardWindow() = default;

void WizardWindow::closeEvent(QCloseEvent* event)
{
    if (worker_thread_ && worker_thread_->isRunning()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void WizardWindow::start_worker_thread(WizardWorker* worker)
{
    cancel_flag_ = std::make_shared<std::atomic<bool>>(false);
    worker->set_cancel_flag(cancel_flag_);
    elapsed_clock_.start();
    step_clock_.start();
    const QString zero = "0:00 / 0:00";
    if      (current_phase_ == 1) page1_->set_elapsed(zero);
    else if (current_phase_ == 2) page2_->set_elapsed(zero);
    else                          page3_->set_elapsed(zero);
    if (!elapsed_timer_) {
        elapsed_timer_ = new QTimer(this);
        elapsed_timer_->setInterval(1000);
        connect(elapsed_timer_, &QTimer::timeout,
                this, &WizardWindow::on_elapsed_tick);
    }
    elapsed_timer_->start();
    worker_thread_ = new QThread;
    worker->moveToThread(worker_thread_);
    connect(worker_thread_, &QThread::started,   worker, &WizardWorker::run);
    connect(worker,  &WizardWorker::progressChanged, this, &WizardWindow::on_progress);
    connect(worker,  &WizardWorker::finished,        this, &WizardWindow::on_worker_finished);
    connect(worker,  &WizardWorker::finished, worker_thread_, &QThread::quit);
    connect(worker_thread_, &QThread::finished, worker,       &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);
    worker_thread_->start();
}

void WizardWindow::on_progress(int step, int total, const QString& name)
{
    if (current_phase_ == 1)      page1_->update_progress(step, total, name);
    else if (current_phase_ == 2) page2_->update_progress(step, total, name);
    else                          page3_->update_progress(step, total, name);
    step_clock_.restart();
}

void WizardWindow::on_worker_finished(modelrepair::Mesh result,
                                       modelrepair::RepairReport report,
                                       QString error)
{
    worker_thread_ = nullptr;
    cancel_flag_   = nullptr;
    if (elapsed_timer_) elapsed_timer_->stop();
    on_elapsed_tick();
    btn_cancel_->setEnabled(true);

    if (error == "__cancelled__") {
        if (current_phase_ == 1) page1_->show_options();
        else if (current_phase_ == 2) page2_->show_options();
        else page3_->show_analysis(current_mesh_);
        return;
    }

    if (!error.isEmpty()) {
        QMessageBox::critical(this, "Phase failed", error);
        // Revert to options state
        if (current_phase_ == 1) page1_->show_options();
        else if (current_phase_ == 2) page2_->show_options();
        else page3_->show_analysis(current_mesh_);
        return;
    }

    const modelrepair::Mesh before = phase_start_mesh_;
    current_mesh_ = std::move(result);

    if (current_phase_ == 1)
        page1_->show_preview(before, current_mesh_, report);
    else if (current_phase_ == 2)
        page2_->show_preview(before, current_mesh_, report);
    else
        page3_->show_preview(before, current_mesh_, report);
}

void WizardWindow::on_cancel_clicked()
{
    if (worker_thread_ && worker_thread_->isRunning()) {
        if (cancel_flag_) {
            cancel_flag_->store(true);
            btn_cancel_->setEnabled(false);
        }
    } else {
        reject();
    }
}

void WizardWindow::on_elapsed_tick()
{
    const int done = (current_phase_ == 1) ? page1_->steps_done()
                   : (current_phase_ == 2) ? page2_->steps_done()
                   :                         page3_->steps_done();
    const int tot  = (current_phase_ == 1) ? page1_->steps_total()
                   : (current_phase_ == 2) ? page2_->steps_total()
                   :                         page3_->steps_total();
    const QString text = gui::eta_text(elapsed_clock_.elapsed(), step_clock_.elapsed(), done, tot);
    if      (current_phase_ == 1) page1_->set_elapsed(text);
    else if (current_phase_ == 2) page2_->set_elapsed(text);
    else                          page3_->set_elapsed(text);
}

// ─── Phase 1 handlers ─────────────────────────────────────────────────────────

void WizardWindow::on_phase1_run()
{
    phase_start_mesh_ = current_mesh_;
    page1_->show_running();
    auto* worker = new WizardWorker(current_mesh_, page1_->collect_options());
    start_worker_thread(worker);
}

void WizardWindow::on_phase1_continue() { enter_phase2(); }
void WizardWindow::on_phase1_finish()   { try_save_and_accept(); }

// ─── Phase 2 handlers ─────────────────────────────────────────────────────────

void WizardWindow::on_phase2_run()
{
    const auto p = page2_->collect_params();
    if (!p.do_remesh && !p.do_smooth && !p.do_subdivide) {
        QMessageBox::information(this, "Nothing selected",
            "Enable at least one of Remesh, Smooth, or Subdivide, or click Skip.");
        return;
    }
    phase_start_mesh_ = current_mesh_;
    page2_->show_running();
    auto* worker = new WizardWorker(current_mesh_,
        p.do_remesh, p.remesh_factor, p.remesh_iters,
        p.do_smooth, p.smooth_iters, p.crease_angle, p.use_vulkan,
        p.do_subdivide, p.subdivide_method, p.subdivide_iters);
    start_worker_thread(worker);
}

void WizardWindow::on_phase2_retry()
{
    current_mesh_ = phase_start_mesh_;
    page2_->show_options();
}

void WizardWindow::on_phase2_skip()     { enter_phase3(); }
void WizardWindow::on_phase2_continue() { enter_phase3(); }
void WizardWindow::on_phase2_finish()   { try_save_and_accept(); }

// ─── Phase 3 handlers ─────────────────────────────────────────────────────────

void WizardWindow::on_phase3_run(double ratio)
{
    phase_start_mesh_ = current_mesh_;
    page3_->show_running();
    auto* worker = new WizardWorker(current_mesh_, ratio);
    start_worker_thread(worker);
}

void WizardWindow::on_phase3_retry()
{
    current_mesh_ = phase_start_mesh_;
    page3_->show_analysis(current_mesh_);
}

void WizardWindow::on_phase3_skip() { accept(); }

void WizardWindow::on_phase3_save() { try_save_and_accept(); }

void WizardWindow::try_save_and_accept()
{
    const auto stem = input_path_.stem().string() + "_repaired";
    const QString suggestion = QString::fromStdString(
        (input_path_.parent_path() / (stem + input_path_.extension().string())).string());

    const QString path = QFileDialog::getSaveFileName(
        this, "Save repaired mesh", suggestion,
        "STL binary (*.stl);;OBJ (*.obj);;3MF (*.3mf);;All files (*)");
    if (path.isEmpty()) return;

    try {
        modelrepair::io::save(current_mesh_, std::filesystem::path(path.toStdString()));
        accept();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Save failed", e.what());
    }
}

// ─── Navigation helpers ───────────────────────────────────────────────────────

void WizardWindow::enter_phase2()
{
    current_phase_ = 2;
    phase_header_->setText("Phase 2 of 3 — Remesh & Smooth (optional)");
    page2_->set_face_count(current_mesh_.num_faces());
    pages_->setCurrentWidget(page2_);
}

void WizardWindow::enter_phase3()
{
    current_phase_ = 3;
    phase_header_->setText("Phase 3 of 3 — Decimate (optional)");
    page3_->show_analysis(current_mesh_);
    pages_->setCurrentWidget(page3_);
}

} // namespace gui

// Required by Qt AUTOMOC for Q_OBJECT classes defined in a .cpp
#include "WizardWindow.moc"

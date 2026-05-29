#include "PreviewWindow.hpp"
#include "MeshViewWidget.hpp"

#include "modelrepair/WallThickness.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui
{

PreviewWindow::PreviewWindow(const modelrepair::Mesh& before,
                             const modelrepair::Mesh& after,
                             QWidget* parent)
    : QWidget(parent, Qt::Window), before_copy_(before), after_copy_(after)
{
    setWindowTitle("Repair Preview — Before / After");
    resize(1200, 700);

    auto shared_camera = std::make_shared<CameraState>();

    before_view_ = new MeshViewWidget(before_copy_, shared_camera, this);
    after_view_  = new MeshViewWidget(after_copy_,  shared_camera, this);

    before_view_->set_peer(after_view_);
    after_view_->set_peer(before_view_);

    auto* lbl_before = new QLabel("Before");
    auto* lbl_after  = new QLabel("After");
    lbl_before->setAlignment(Qt::AlignCenter);
    lbl_after ->setAlignment(Qt::AlignCenter);

    auto* left_col = new QVBoxLayout;
    left_col->addWidget(lbl_before);
    left_col->addWidget(before_view_, 1);

    auto* right_col = new QVBoxLayout;
    right_col->addWidget(lbl_after);
    right_col->addWidget(after_view_, 1);

    auto* views_row = new QHBoxLayout;
    views_row->addLayout(left_col);
    views_row->addLayout(right_col);

    // Shading mode selector
    mode_combo_ = new QComboBox;
    mode_combo_->addItem("Normal");
    mode_combo_->addItem("Wall Thickness");
    mode_combo_->addItem("Overhang");
    connect(mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewWindow::on_display_mode_changed);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel("Shading:"));
    toolbar->addWidget(mode_combo_);
    toolbar->addStretch();

    auto* btn_close = new QPushButton("Close");
    connect(btn_close, &QPushButton::clicked, this, &QWidget::close);

    auto* root = new QVBoxLayout(this);
    root->addLayout(toolbar);
    root->addLayout(views_row, 1);
    root->addWidget(btn_close, 0, Qt::AlignRight);
}

void PreviewWindow::on_display_mode_changed(int index)
{
    if (index == 0) {
        before_view_->set_display_mode(MeshViewWidget::DisplayMode::Normal);
        after_view_ ->set_display_mode(MeshViewWidget::DisplayMode::Normal);
        return;
    }

    if (index == 2) {
        before_view_->set_display_mode(MeshViewWidget::DisplayMode::Overhang);
        after_view_ ->set_display_mode(MeshViewWidget::DisplayMode::Overhang);
        return;
    }

    // Wall thickness (index == 1) — compute for both meshes
    auto compute_scalars = [](const modelrepair::Mesh& mesh) -> std::vector<float> {
        auto raw = modelrepair::analyze_wall_thickness(mesh);
        if (raw.empty())
            return {};

        // Filter out "no hit" values for percentile calculation
        std::vector<double> valid;
        valid.reserve(raw.size());
        for (double v : raw) {
            if (v < 1e10)
                valid.push_back(v);
        }

        if (valid.empty())
            return std::vector<float>(raw.size(), 0.0f);

        std::sort(valid.begin(), valid.end());
        const double cap = valid[valid.size() * 95 / 100]; // 95th percentile
        const double lo  = valid.front();
        const double range = (cap > lo) ? (cap - lo) : 1.0;

        // Map: thin=red (1.0), thick=blue (0.0)
        std::vector<float> scalars(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            double t = std::min(raw[i], cap);
            scalars[i] = 1.0f - static_cast<float>((t - lo) / range);
            scalars[i] = std::clamp(scalars[i], 0.0f, 1.0f);
        }
        return scalars;
    };

    before_view_->set_display_mode(MeshViewWidget::DisplayMode::WallThickness,
                                   compute_scalars(before_copy_));
    after_view_ ->set_display_mode(MeshViewWidget::DisplayMode::WallThickness,
                                   compute_scalars(after_copy_));
}

} // namespace gui

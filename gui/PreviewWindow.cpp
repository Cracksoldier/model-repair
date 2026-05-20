#include "PreviewWindow.hpp"
#include "MeshViewWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

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

    auto* before_view = new MeshViewWidget(before_copy_, shared_camera, this);
    auto* after_view  = new MeshViewWidget(after_copy_,  shared_camera, this);

    before_view->set_peer(after_view);
    after_view->set_peer(before_view);

    auto* lbl_before = new QLabel("Before");
    auto* lbl_after  = new QLabel("After");
    lbl_before->setAlignment(Qt::AlignCenter);
    lbl_after ->setAlignment(Qt::AlignCenter);

    auto* left_col = new QVBoxLayout;
    left_col->addWidget(lbl_before);
    left_col->addWidget(before_view, 1);

    auto* right_col = new QVBoxLayout;
    right_col->addWidget(lbl_after);
    right_col->addWidget(after_view, 1);

    auto* views_row = new QHBoxLayout;
    views_row->addLayout(left_col);
    views_row->addLayout(right_col);

    auto* btn_close = new QPushButton("Close");
    connect(btn_close, &QPushButton::clicked, this, &QWidget::close);

    auto* root = new QVBoxLayout(this);
    root->addLayout(views_row, 1);
    root->addWidget(btn_close, 0, Qt::AlignRight);
}

} // namespace gui

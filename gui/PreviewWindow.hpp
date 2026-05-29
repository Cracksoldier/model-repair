#pragma once

#include "modelrepair/Mesh.hpp"
#include "MeshViewWidget.hpp"

#include <QWidget>

class QComboBox;

namespace gui
{

class PreviewWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewWindow(const modelrepair::Mesh& before,
                           const modelrepair::Mesh& after,
                           QWidget* parent = nullptr);

private slots:
    void on_display_mode_changed(int index);

private:
    modelrepair::Mesh before_copy_;
    modelrepair::Mesh after_copy_;

    MeshViewWidget* before_view_ = nullptr;
    MeshViewWidget* after_view_  = nullptr;
    QComboBox*      mode_combo_  = nullptr;
};

} // namespace gui

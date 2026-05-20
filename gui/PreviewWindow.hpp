#pragma once

#include "modelrepair/Mesh.hpp"

#include <QWidget>

namespace gui
{

class PreviewWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewWindow(const modelrepair::Mesh& before,
                           const modelrepair::Mesh& after,
                           QWidget* parent = nullptr);

private:
    modelrepair::Mesh before_copy_;
    modelrepair::Mesh after_copy_;
};

} // namespace gui

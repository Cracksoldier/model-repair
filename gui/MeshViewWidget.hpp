#pragma once

#include "modelrepair/Mesh.hpp"

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QQuaternion>
#include <QVector3D>
#include <QPointF>
#include <memory>
#include <vector>

namespace gui
{

struct CameraState
{
    QQuaternion rotation;
    float       zoom         = 1.0f;
    QVector3D   pan;
    float       scene_radius = 1.0f;
};

class MeshViewWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit MeshViewWidget(const modelrepair::Mesh& mesh,
                            std::shared_ptr<CameraState> camera,
                            QWidget* parent = nullptr);
    ~MeshViewWidget() override;

    void set_peer(MeshViewWidget* peer);

    enum class DisplayMode { Normal, WallThickness, Overhang };
    void set_display_mode(DisplayMode mode, const std::vector<float>& per_face_scalars = {});

signals:
    void camera_changed();

protected:
    void initializeGL()         override;
    void resizeGL(int w, int h) override;
    void paintGL()              override;

    void mousePressEvent  (QMouseEvent* e) override;
    void mouseMoveEvent   (QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent       (QWheelEvent* e) override;

private:
    void upload_mesh();
    void auto_fit();

    const modelrepair::Mesh&       mesh_;
    std::shared_ptr<CameraState>   camera_;
    MeshViewWidget*                peer_ = nullptr;

    QOpenGLBuffer            vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao_;
    QOpenGLShaderProgram     program_;

    int     vertex_count_ = 0;
    float   aspect_       = 1.0f;
    QPointF last_mouse_;
    bool    drag_left_  = false;
    bool    drag_right_ = false;

    DisplayMode         display_mode_     = DisplayMode::Normal;
    std::vector<float>  per_face_scalars_;

    int u_mvp_          = -1;
    int u_model_        = -1;
    int u_normal_mat_   = -1;
    int u_light_dir_    = -1;
    int u_color_        = -1;
    int u_display_mode_ = -1;
};

} // namespace gui

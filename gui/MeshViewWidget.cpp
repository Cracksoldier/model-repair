#include "MeshViewWidget.hpp"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QMatrix4x4>
#include <QMatrix3x3>

#include <algorithm>
#include <cmath>
#include <vector>

namespace gui
{

static const char* k_vert_src = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat3 u_normal_mat;
out vec3 v_normal_world;
void main() {
    v_normal_world = normalize(u_normal_mat * a_normal);
    gl_Position    = u_mvp * vec4(a_pos, 1.0);
}
)";

static const char* k_frag_src = R"(
#version 330 core
in  vec3 v_normal_world;
out vec4 frag_color;
uniform vec3 u_light_dir;
uniform vec3 u_color;
void main() {
    vec3 N = normalize(v_normal_world);
    float d = abs(dot(N, normalize(u_light_dir)));
    vec3 col = 0.15 * u_color + 0.75 * d * u_color;
    frag_color = vec4(col, 1.0);
}
)";

MeshViewWidget::MeshViewWidget(const modelrepair::Mesh& mesh,
                               std::shared_ptr<CameraState> camera,
                               QWidget* parent)
    : QOpenGLWidget(parent), mesh_(mesh), camera_(std::move(camera))
{
    setMouseTracking(false);
}

MeshViewWidget::~MeshViewWidget()
{
    makeCurrent();
    vbo_.destroy();
    vao_.destroy();
    doneCurrent();
}

void MeshViewWidget::set_peer(MeshViewWidget* peer)
{
    peer_ = peer;
    connect(peer, &MeshViewWidget::camera_changed, this, [this]() { update(); });
}

void MeshViewWidget::initializeGL()
{
    initializeOpenGLFunctions();

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex,   k_vert_src);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, k_frag_src);
    program_.link();

    u_mvp_        = program_.uniformLocation("u_mvp");
    u_model_      = program_.uniformLocation("u_model");
    u_normal_mat_ = program_.uniformLocation("u_normal_mat");
    u_light_dir_  = program_.uniformLocation("u_light_dir");
    u_color_      = program_.uniformLocation("u_color");

    vao_.create();
    vbo_.create();
    vbo_.setUsagePattern(QOpenGLBuffer::StaticDraw);

    upload_mesh();
    auto_fit();
}

void MeshViewWidget::resizeGL(int w, int h)
{
    aspect_ = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
}

void MeshViewWidget::paintGL()
{
    glClearColor(0.18f, 0.18f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (vertex_count_ == 0)
        return;

    float r        = camera_->scene_radius;
    float eye_dist = 2.5f * r / camera_->zoom;

    QMatrix4x4 proj;
    proj.perspective(45.0f, aspect_, 0.01f * r, 100.0f * r);

    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -eye_dist);
    view.rotate(camera_->rotation);

    QMatrix4x4 model;
    model.translate(camera_->pan);

    QMatrix4x4 mv  = view * model;
    QMatrix4x4 mvp = proj * mv;
    QMatrix3x3 nm  = mv.normalMatrix();

    glEnable(GL_DEPTH_TEST);

    program_.bind();
    program_.setUniformValue(u_mvp_,        mvp);
    program_.setUniformValue(u_model_,      mv);
    program_.setUniformValue(u_normal_mat_, nm);
    program_.setUniformValue(u_light_dir_,  QVector3D(1.0f, 1.5f, 2.0f).normalized());
    program_.setUniformValue(u_color_,      QVector3D(0.72f, 0.72f, 0.78f));

    vao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, vertex_count_);
    vao_.release();
    program_.release();
}

void MeshViewWidget::upload_mesh()
{
    const auto& sm = mesh_.cgal();

    std::vector<float> data;
    data.reserve(sm.number_of_faces() * 3 * 6);

    for (auto f : sm.faces())
    {
        auto he = sm.halfedge(f);
        auto v0 = sm.source(he);
        auto v1 = sm.target(he);
        auto v2 = sm.target(sm.next(he));

        const auto& p0 = sm.point(v0);
        const auto& p1 = sm.point(v1);
        const auto& p2 = sm.point(v2);

        auto   cn  = CGAL::cross_product(p1 - p0, p2 - p0);
        double len = std::sqrt(CGAL::to_double(cn.squared_length()));
        float nx, ny, nz;
        if (len > 1e-12) {
            nx = static_cast<float>(CGAL::to_double(cn.x()) / len);
            ny = static_cast<float>(CGAL::to_double(cn.y()) / len);
            nz = static_cast<float>(CGAL::to_double(cn.z()) / len);
        } else {
            nx = 0.0f; ny = 0.0f; nz = 1.0f;
        }

        for (const auto* p : {&p0, &p1, &p2}) {
            data.push_back(static_cast<float>(CGAL::to_double(p->x())));
            data.push_back(static_cast<float>(CGAL::to_double(p->y())));
            data.push_back(static_cast<float>(CGAL::to_double(p->z())));
            data.push_back(nx);
            data.push_back(ny);
            data.push_back(nz);
        }
    }

    vertex_count_ = static_cast<int>(data.size() / 6);

    program_.bind();
    vao_.bind();
    vbo_.bind();
    vbo_.allocate(data.data(), static_cast<int>(data.size() * sizeof(float)));

    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0,                3, 6 * sizeof(float));
    program_.enableAttributeArray(1);
    program_.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, 6 * sizeof(float));

    vao_.release();
    vbo_.release();
    program_.release();
}

void MeshViewWidget::auto_fit()
{
    const auto& sm = mesh_.cgal();
    if (sm.number_of_vertices() == 0)
        return;

    double xmin =  1e30, xmax = -1e30;
    double ymin =  1e30, ymax = -1e30;
    double zmin =  1e30, zmax = -1e30;

    for (auto v : sm.vertices()) {
        const auto& p = sm.point(v);
        double x = CGAL::to_double(p.x());
        double y = CGAL::to_double(p.y());
        double z = CGAL::to_double(p.z());
        xmin = std::min(xmin, x); xmax = std::max(xmax, x);
        ymin = std::min(ymin, y); ymax = std::max(ymax, y);
        zmin = std::min(zmin, z); zmax = std::max(zmax, z);
    }

    float cx = static_cast<float>((xmin + xmax) / 2.0);
    float cy = static_cast<float>((ymin + ymax) / 2.0);
    float cz = static_cast<float>((zmin + zmax) / 2.0);

    double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
    float radius = static_cast<float>(std::sqrt(dx*dx + dy*dy + dz*dz) / 2.0);

    camera_->pan          = QVector3D(-cx, -cy, -cz);
    camera_->scene_radius = (radius > 1e-6f) ? radius : 1.0f;
    camera_->zoom         = 1.0f;
    camera_->rotation     = QQuaternion();
}

void MeshViewWidget::mousePressEvent(QMouseEvent* e)
{
    last_mouse_ = e->position();
    if (e->button() == Qt::LeftButton)  drag_left_  = true;
    if (e->button() == Qt::RightButton) drag_right_ = true;
}

void MeshViewWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)  drag_left_  = false;
    if (e->button() == Qt::RightButton) drag_right_ = false;
}

void MeshViewWidget::mouseMoveEvent(QMouseEvent* e)
{
    QPointF delta = e->position() - last_mouse_;
    last_mouse_   = e->position();

    if (drag_left_)
    {
        float ax = static_cast<float>(delta.y()) * 0.4f;
        float ay = static_cast<float>(delta.x()) * 0.4f;
        QQuaternion rx = QQuaternion::fromAxisAndAngle(1, 0, 0, ax);
        QQuaternion ry = QQuaternion::fromAxisAndAngle(0, 1, 0, ay);
        camera_->rotation = (ry * rx * camera_->rotation).normalized();
    }
    else if (drag_right_)
    {
        float scale = 2.0f * camera_->scene_radius / (camera_->zoom * static_cast<float>(height()));
        camera_->pan += QVector3D(
            static_cast<float>( delta.x()) * scale,
            static_cast<float>(-delta.y()) * scale,
            0.0f);
    }

    update();
    emit camera_changed();
}

void MeshViewWidget::wheelEvent(QWheelEvent* e)
{
    float steps = static_cast<float>(e->angleDelta().y()) / 120.0f;
    camera_->zoom *= std::pow(1.15f, steps);
    camera_->zoom  = std::clamp(camera_->zoom, 0.01f, 100.0f);
    update();
    emit camera_changed();
}

} // namespace gui

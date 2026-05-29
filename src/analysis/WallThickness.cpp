#include "modelrepair/WallThickness.hpp"

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>

#include <cmath>
#include <limits>

namespace modelrepair
{

namespace
{
    using Primitive = CGAL::AABB_face_graph_triangle_primitive<SurfMesh>;
    using Traits    = CGAL::AABB_traits<Kernel, Primitive>;
    using Tree      = CGAL::AABB_tree<Traits>;
    using Ray3      = Kernel::Ray_3;
    using Vector3   = Kernel::Vector_3;
} // namespace

std::vector<double> analyze_wall_thickness(const Mesh& mesh)
{
    const auto& sm = mesh.cgal();
    if (sm.number_of_faces() == 0)
        return {};

    Tree tree(faces(sm).first, faces(sm).second, sm);
    tree.accelerate_distance_queries();

    std::vector<double> result;
    result.reserve(sm.number_of_faces());

    const double eps = 1e-5;

    for (auto f : sm.faces())
    {
        auto he = sm.halfedge(f);
        auto v0 = sm.source(he);
        auto v1 = sm.target(he);
        auto v2 = sm.target(sm.next(he));

        const auto& p0 = sm.point(v0);
        const auto& p1 = sm.point(v1);
        const auto& p2 = sm.point(v2);

        // Outward face normal
        auto   cn  = CGAL::cross_product(p1 - p0, p2 - p0);
        double len = std::sqrt(CGAL::to_double(cn.squared_length()));

        if (len < 1e-12) {
            result.push_back(std::numeric_limits<double>::max());
            continue;
        }

        Vector3 outward(CGAL::to_double(cn.x()) / len,
                        CGAL::to_double(cn.y()) / len,
                        CGAL::to_double(cn.z()) / len);

        Point3 centroid = CGAL::centroid(p0, p1, p2);

        // Origin slightly outside the face, direction pointing inward
        Point3  origin(CGAL::to_double(centroid.x()) + eps * outward.x(),
                       CGAL::to_double(centroid.y()) + eps * outward.y(),
                       CGAL::to_double(centroid.z()) + eps * outward.z());
        Vector3 inward(-outward.x(), -outward.y(), -outward.z());
        Ray3    ray(origin, inward);

        auto hit = tree.first_intersection(ray);
        if (!hit) {
            result.push_back(std::numeric_limits<double>::max());
            continue;
        }

        // Extract the intersection point from the variant result
        double thickness = std::numeric_limits<double>::max();
        auto visitor = [&](const auto& geom) {
            using T = std::decay_t<decltype(geom)>;
            Point3 pt;
            if constexpr (std::is_same_v<T, Point3>) {
                pt = geom;
            } else {
                // Segment_3 — take the first endpoint
                pt = geom.source();
            }
            double dx = CGAL::to_double(pt.x()) - CGAL::to_double(centroid.x());
            double dy = CGAL::to_double(pt.y()) - CGAL::to_double(centroid.y());
            double dz = CGAL::to_double(pt.z()) - CGAL::to_double(centroid.z());
            thickness = std::sqrt(dx*dx + dy*dy + dz*dz);
        };
        std::visit(visitor, hit->first);

        result.push_back(thickness);
    }

    return result;
}

} // namespace modelrepair

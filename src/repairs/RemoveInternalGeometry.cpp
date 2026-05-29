#include "modelrepair/RemoveInternalGeometry.hpp"

#include <CGAL/Side_of_triangle_mesh.h>

#include <chrono>
#include <vector>

namespace modelrepair
{

RemoveInternalGeometryResult remove_internal_geometry(Mesh& mesh)
{
    auto& sm = mesh.cgal();
    RemoveInternalGeometryResult r;
    r.faces_before = sm.number_of_faces();

    if (!mesh.is_closed()) {
        r.faces_after = r.faces_before;
        return r;
    }

    auto t0 = std::chrono::steady_clock::now();

    CGAL::Side_of_triangle_mesh<SurfMesh, Kernel> inside_test(sm);

    std::vector<SurfMesh::Face_index> to_remove;

    for (auto f : sm.faces())
    {
        auto he = sm.halfedge(f);
        const auto& p0 = sm.point(sm.source(he));
        const auto& p1 = sm.point(sm.target(he));
        const auto& p2 = sm.point(sm.target(sm.next(he)));

        Point3 centroid = CGAL::centroid(p0, p1, p2);

        if (inside_test(centroid) == CGAL::ON_BOUNDED_SIDE)
            to_remove.push_back(f);
    }

    r.faces_removed = to_remove.size();

    for (auto f : to_remove)
        sm.remove_face(f);

    if (r.faces_removed > 0)
        sm.collect_garbage();

    r.faces_after = sm.number_of_faces();

    auto t1 = std::chrono::steady_clock::now();
    r.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

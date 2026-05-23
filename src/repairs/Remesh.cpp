#include "modelrepair/Remesh.hpp"

#include <CGAL/Polygon_mesh_processing/remesh.h>

#include <chrono>
#include <cmath>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

RemeshResult remesh(Mesh& mesh, double edge_length_factor, unsigned int iterations)
{
    auto t0 = std::chrono::steady_clock::now();

    SurfMesh& M = mesh.cgal();

    RemeshResult r;
    r.faces_before = M.number_of_faces();

    // Compute mean edge length to derive the isotropic target.
    double total = 0.0;
    std::size_t n = 0;
    for (auto e : edges(M)) {
        auto h = halfedge(e, M);
        total += std::sqrt(CGAL::to_double(
            (M.point(target(h, M)) - M.point(source(h, M))).squared_length()));
        ++n;
    }
    double target_len = (n > 0 ? total / n : 1.0) * edge_length_factor;

    // Mark fine edges (length <= target_len) as constrained so the remesher
    // leaves them and their incident vertices completely untouched.
    // Coarse edges (length > target_len) are free: CGAL splits them toward
    // target_len, redistributing geometry in blocky regions.
    // Passing faces(M) instead of a face subset avoids the is_on_patch_border
    // assertion that fires when the coarse-face patch has a pinch-point topology.
    auto [ecm, ecm_ok] = M.add_property_map<SurfMesh::Edge_index, bool>(
                            "e:remesh_constrained", false);
    (void)ecm_ok;
    for (auto e : edges(M)) {
        auto h = halfedge(e, M);
        double elen = std::sqrt(CGAL::to_double(
            (M.point(target(h, M)) - M.point(source(h, M))).squared_length()));
        if (elen <= target_len)
            ecm[e] = true;
    }

    PMP::isotropic_remeshing(faces(M), target_len, M,
        CGAL::parameters::number_of_iterations(iterations)
                           .edge_is_constrained_map(ecm)
                           .protect_constraints(true));

    M.remove_property_map(ecm);

    auto t1 = std::chrono::steady_clock::now();

    r.faces_after  = M.number_of_faces();
    r.duration_ms  = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

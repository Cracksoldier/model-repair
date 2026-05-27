#include "modelrepair/Remesh.hpp"

#include <CGAL/Polygon_mesh_processing/compute_normal.h>
#include <CGAL/Polygon_mesh_processing/remesh.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

RemeshResult remesh(Mesh& mesh, double edge_length_factor, unsigned int iterations,
                    double sharp_feature_angle,
                    std::function<void(unsigned int)> on_iteration)
{
    using V3 = Kernel::Vector_3;

    auto t0 = std::chrono::steady_clock::now();

    SurfMesh& M = mesh.cgal();

    RemeshResult r;
    r.faces_before = M.number_of_faces();

    // Edges sharper than sharp_feature_angle are auto-constrained — preserves
    // hard creases / mechanical edges regardless of their length.
    const double cos_sharp = std::cos(sharp_feature_angle * std::numbers::pi / 180.0);

    // Capture volume once before all iterations; restore once at the end.
    // PMP::isotropic_remeshing shrinks closed meshes by 1–3 % through vertex
    // relocation. Restoration mirrors the pattern in src/repairs/Smooth.cpp.
    auto vol_before = mesh.volume(); // nullopt if open

    for (unsigned int iter = 0; iter < iterations; ++iter)
    {
        // Median edge length — robust on bimodal meshes (fine face + coarse
        // body). Mean is dragged up by long coarse edges, pushing target_len
        // too high and leaving the body too coarsely sampled.
        std::vector<double> elens;
        elens.reserve(M.number_of_edges());
        for (auto e : edges(M)) {
            auto h = halfedge(e, M);
            elens.push_back(std::sqrt(CGAL::to_double(
                (M.point(target(h, M)) - M.point(source(h, M))).squared_length())));
        }
        double median_len = 1.0;
        if (!elens.empty()) {
            auto mid = elens.begin() + elens.size() / 2;
            std::nth_element(elens.begin(), mid, elens.end());
            median_len = *mid;
        }
        const double target_len = median_len * edge_length_factor;

        // CGAL's protect_constraints(true) requires every constrained edge to be
        // ≤ 4/3 × target_len. Edges above this threshold are left unconstrained;
        // CGAL splits them into shorter pieces, which then satisfy the precondition
        // and get constrained on subsequent iterations.
        const double max_protected = (4.0 / 3.0) * target_len;

        // Per-iteration face normals for sharp-edge detection.
        auto [fnormals, fn_ok] = M.add_property_map<SurfMesh::Face_index, V3>(
                                    "f:remesh_normal");
        (void)fn_ok;
        PMP::compute_face_normals(M, fnormals);

        // Mark constrained edges: short (fine geometry) OR sharp (hard creases).
        auto [ecm, ecm_ok] = M.add_property_map<SurfMesh::Edge_index, bool>(
                                "e:remesh_constrained", false);
        (void)ecm_ok;
        for (auto e : edges(M)) {
            auto h = halfedge(e, M);
            double elen = std::sqrt(CGAL::to_double(
                (M.point(target(h, M)) - M.point(source(h, M))).squared_length()));

            bool is_fine  = (elen <= target_len);
            bool is_sharp = false;
            auto f1 = face(h, M);
            auto f2 = face(opposite(h, M), M);
            if (f1 != SurfMesh::null_face() && f2 != SurfMesh::null_face()) {
                const V3& n1 = fnormals[f1];
                const V3& n2 = fnormals[f2];
                double len = std::sqrt(CGAL::to_double(n1 * n1))
                           * std::sqrt(CGAL::to_double(n2 * n2));
                double dot_norm = (len < 1e-12) ? -1.0
                                                : CGAL::to_double(n1 * n2) / len;
                if (dot_norm < cos_sharp) is_sharp = true;
            }
            if ((is_fine || is_sharp) && elen <= max_protected) ecm[e] = true;
        }

        // protect_constraints(true) enforces an extra precondition that ALL edges
        // on the patch border (including the mesh's open boundary) be ≤ 4/3 ×
        // target_len. Open meshes with long border edges would crash CGAL; for
        // those we relax the strict mode. Closed meshes (typical post-repair
        // case) keep strict feature preservation.
        const bool strict = mesh.is_closed();
        PMP::isotropic_remeshing(faces(M), target_len, M,
            CGAL::parameters::number_of_iterations(1)
                               .edge_is_constrained_map(ecm)
                               .protect_constraints(strict));

        M.remove_property_map(fnormals);
        M.remove_property_map(ecm);

        if (on_iteration) on_iteration(iter + 1);
    }

    // Restore original volume via uniform scaling around the centroid.
    if (vol_before && *vol_before > 1e-10) {
        auto vol_after = mesh.volume();
        if (vol_after && *vol_after > 1e-10) {
            double scale = std::cbrt(*vol_before / *vol_after);
            V3 centroid = CGAL::NULL_VECTOR;
            for (auto v : vertices(M))
                centroid = centroid + (M.point(v) - CGAL::ORIGIN);
            centroid = centroid / static_cast<double>(M.num_vertices());
            Point3 c = CGAL::ORIGIN + centroid;
            for (auto v : vertices(M))
                M.point(v) = c + scale * (M.point(v) - c);
        }
    }

    auto t1 = std::chrono::steady_clock::now();

    r.faces_after  = M.number_of_faces();
    r.duration_ms  = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

#include "modelrepair/Decimate.hpp"

#include <CGAL/Surface_mesh_simplification/edge_collapse.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Face_count_ratio_stop_predicate.h>

#include <chrono>

namespace SMS = CGAL::Surface_mesh_simplification;

namespace modelrepair
{

DecimateResult decimate(Mesh& mesh, double ratio)
{
    auto& sm = mesh.cgal();
    DecimateResult r;
    r.faces_before = sm.number_of_faces();

    auto t0 = std::chrono::steady_clock::now();
    SMS::edge_collapse(sm, SMS::Face_count_ratio_stop_predicate<SurfMesh>(ratio, sm));
    auto t1 = std::chrono::steady_clock::now();

    r.faces_after = sm.number_of_faces();
    r.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

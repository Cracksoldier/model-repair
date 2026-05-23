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

    PMP::isotropic_remeshing(faces(M), target_len, M,
        CGAL::parameters::number_of_iterations(iterations));

    auto t1 = std::chrono::steady_clock::now();

    r.faces_after  = M.number_of_faces();
    r.duration_ms  = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

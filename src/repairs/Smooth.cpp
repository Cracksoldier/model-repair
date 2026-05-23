#include "modelrepair/Smooth.hpp"

#include <CGAL/Polygon_mesh_processing/angle_and_area_smoothing.h>

#include <chrono>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

SmoothResult smooth(Mesh& mesh, unsigned int iterations)
{
    auto t0 = std::chrono::steady_clock::now();

    PMP::angle_and_area_smoothing(mesh.cgal(),
        CGAL::parameters::number_of_iterations(iterations)
                        .use_area_smoothing(false)
                        .use_Delaunay_flips(true)
                        .use_safety_constraints(false));

    auto t1 = std::chrono::steady_clock::now();

    SmoothResult r;
    r.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

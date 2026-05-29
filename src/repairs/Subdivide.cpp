#include "modelrepair/Subdivide.hpp"

#include <CGAL/subdivision_method_3.h>

#include <chrono>

namespace SMS3 = CGAL::Subdivision_method_3;

namespace modelrepair
{

SubdivisionResult subdivide(Mesh& mesh, unsigned int iterations, SubdivisionMethod method)
{
    auto& sm = mesh.cgal();
    SubdivisionResult r;
    r.faces_before = sm.number_of_faces();

    auto t0 = std::chrono::steady_clock::now();

    auto params = CGAL::parameters::number_of_iterations(iterations);
    switch (method) {
        case SubdivisionMethod::Loop:
            SMS3::Loop_subdivision(sm, params);
            break;
        case SubdivisionMethod::CatmullClark:
            SMS3::CatmullClark_subdivision(sm, params);
            break;
    }

    auto t1 = std::chrono::steady_clock::now();
    r.faces_after  = sm.number_of_faces();
    r.duration_ms  = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

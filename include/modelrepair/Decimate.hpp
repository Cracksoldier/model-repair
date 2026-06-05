#pragma once

#include "Mesh.hpp"
#include <cstddef>

namespace modelrepair
{

enum class DecimateBackend
{
    CGAL,           // edge_collapse (exact, slow)
    MeshOptimizer,  // meshopt_simplify (fast, MIT)
    OpenMesh,       // DecimaterT + ModQuadricT (LGPL-3.0)
};

struct DecimateParams
{
    DecimateBackend backend          = DecimateBackend::CGAL;
    double          ratio            = 0.5;   // fraction of faces to retain (0 < ratio ≤ 1)
    double          target_error     = 0.01;  // meshoptimizer: relative geometric error budget
    double          normal_deviation = 15.0;  // OpenMesh: ModNormalDeviationT threshold (degrees)
};

struct DecimateResult
{
    std::size_t     faces_before = 0;
    std::size_t     faces_after  = 0;
    double          duration_ms  = 0.0;
    DecimateBackend backend_used = DecimateBackend::CGAL;
};

// Returns true when built with MODELREPAIR_HAVE_MESHOPTIMIZER.
bool decimate_meshoptimizer_available();

// Returns true when built with MODELREPAIR_HAVE_OPENMESH.
bool decimate_openmesh_available();

// Backward-compatible overload — always uses CGAL backend.
DecimateResult decimate(Mesh& mesh, double ratio);

// Full overload — backend selected via params.backend.
// If the requested backend was not compiled in, falls back to CGAL.
DecimateResult decimate(Mesh& mesh, const DecimateParams& params);

} // namespace modelrepair

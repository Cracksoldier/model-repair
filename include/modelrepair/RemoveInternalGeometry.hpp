#pragma once

#include "Mesh.hpp"
#include <cstddef>

namespace modelrepair
{

struct RemoveInternalGeometryResult
{
    std::size_t faces_before  = 0;
    std::size_t faces_removed = 0;
    std::size_t faces_after   = 0;
    double      duration_ms   = 0.0;
};

// Remove faces whose centroid lies inside the closed mesh.
// Returns early (faces_removed = 0) if the mesh is not closed.
RemoveInternalGeometryResult remove_internal_geometry(Mesh& mesh);

} // namespace modelrepair

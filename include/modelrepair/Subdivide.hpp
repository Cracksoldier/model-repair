#pragma once

#include "Mesh.hpp"
#include <cstddef>

namespace modelrepair
{

enum class SubdivisionMethod
{
    Loop,         // optimised for triangle meshes
    CatmullClark, // works on any polygon mesh
};

struct SubdivisionResult
{
    std::size_t faces_before = 0;
    std::size_t faces_after  = 0;
    double      duration_ms  = 0.0;
};

// Subdivide mesh in-place. Face count grows ~4× per iteration.
SubdivisionResult subdivide(Mesh& mesh,
                            unsigned int      iterations = 1,
                            SubdivisionMethod method     = SubdivisionMethod::Loop);

} // namespace modelrepair

#pragma once

#include "Mesh.hpp"

#include <cstddef>
#include <functional>

namespace modelrepair
{

struct RemeshResult
{
    std::size_t faces_before = 0;
    std::size_t faces_after  = 0;
    double      duration_ms  = 0.0;
};

// sharp_feature_angle: edges with a dihedral angle greater than this (degrees)
// are auto-constrained — preserves hard creases / mechanical edges regardless
// of their length. Default 45° matches the smooth crease default.
// on_iteration, if provided, is invoked after each completed iteration with the
// 1-based iteration index. Used by the GUI to drive per-iteration progress.
RemeshResult remesh(Mesh& mesh, double edge_length_factor = 0.8,
                    unsigned int iterations = 3,
                    double sharp_feature_angle = 45.0,
                    std::function<void(unsigned int)> on_iteration = nullptr);

} // namespace modelrepair

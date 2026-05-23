#pragma once

#include "Mesh.hpp"

#include <cstddef>

namespace modelrepair
{

struct RemeshResult
{
    std::size_t faces_before = 0;
    std::size_t faces_after  = 0;
    double      duration_ms  = 0.0;
};

RemeshResult remesh(Mesh& mesh, double edge_length_factor = 0.8,
                    unsigned int iterations = 3);

} // namespace modelrepair

#pragma once

#include "Mesh.hpp"
#include <cstddef>

namespace modelrepair
{

struct DecimateResult
{
    std::size_t faces_before = 0;
    std::size_t faces_after  = 0;
    double      duration_ms  = 0.0;
};

DecimateResult decimate(Mesh& mesh, double ratio);

} // namespace modelrepair

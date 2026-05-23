#pragma once

#include "Mesh.hpp"

namespace modelrepair
{

struct SmoothResult
{
    double duration_ms = 0.0;
};

SmoothResult smooth(Mesh& mesh, unsigned int iterations);

} // namespace modelrepair

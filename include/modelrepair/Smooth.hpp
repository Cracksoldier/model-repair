#pragma once

#include "Mesh.hpp"

namespace modelrepair
{

struct SmoothResult
{
    double duration_ms = 0.0;
};

SmoothResult smooth(Mesh& mesh, unsigned int iterations, double crease_angle = 45.0);

} // namespace modelrepair

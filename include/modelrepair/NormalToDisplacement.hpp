#pragma once

#include <string>
#include <vector>

namespace modelrepair
{

struct NormalToDisplacementSettings
{
    bool  flip_green        = false;
    bool  invert_height     = false;
    float gradient_strength = 1.0f;
    float contrast          = 1.0f;
    float blur_radius       = 0.0f;   // pixels; 0 = no blur
    int   solver_max_iter   = 1000;
    bool  normalize_output  = true;
};

struct NormalToDisplacementResult
{
    std::vector<float> height;   // width * height_px floats in [0, 1]
    int   width       = 0;
    int   height_px   = 0;
    float duration_ms = 0.f;
};

// Convert a tangent-space normal map to a grayscale height map via Poisson
// reconstruction of the gradient field derived from the normal vectors.
// No mesh is required — this is a pure image-in / image-out operation.
// Throws std::runtime_error if the image cannot be loaded.
NormalToDisplacementResult normal_to_displacement(
    const std::string& normal_map_path,
    const NormalToDisplacementSettings& settings = {});

} // namespace modelrepair

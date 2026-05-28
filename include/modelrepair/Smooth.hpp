#pragma once

#include "Mesh.hpp"

#include <functional>

namespace modelrepair
{

struct SmoothResult
{
    double duration_ms = 0.0;
};

// Cotangent-weighted feature-preserving Laplacian smoothing.
// When use_vulkan is true and a Vulkan device is available (see
// smooth_vulkan_available()), the computation runs on the GPU; otherwise the
// CPU parallel path (TBB) or sequential path is used.
SmoothResult smooth(Mesh& mesh, unsigned int iterations,
                    double crease_angle = 45.0,
                    std::function<void(unsigned int)> on_iteration = nullptr,
                    bool use_vulkan = false);

// Returns true iff the binary was built with MODELREPAIR_HAVE_VULKAN and at
// least one Vulkan-capable device is enumerable at runtime. The result is
// cached after the first call.
bool smooth_vulkan_available();

} // namespace modelrepair

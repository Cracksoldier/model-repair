#pragma once
#ifdef MODELREPAIR_HAVE_VULKAN

#include "modelrepair/Mesh.hpp"
#include <functional>

namespace modelrepair {

// GPU-accelerated cotangent Laplacian smoothing via Vulkan compute.
// Weights (cotangent × crease-falloff) are precomputed once from the initial
// mesh and uploaded to VRAM; all iterations run without CPU↔GPU round-trips.
class VulkanSmoothing {
public:
    // Builds CSR weight matrix from the current mesh and initialises the Vulkan
    // pipeline. valid() returns false if device enumeration or setup failed.
    explicit VulkanSmoothing(Mesh& mesh, double crease_angle, float lambda = 0.5f);
    ~VulkanSmoothing();

    bool valid() const;

    // Run n_iterations on the GPU. on_iteration(i) is called after each
    // completed iteration with its 1-based index. Call download() afterwards.
    void run(unsigned int n_iterations,
             std::function<void(unsigned int)> on_iteration = nullptr);

    // Copy final GPU positions back into the CGAL mesh.
    void download(Mesh& mesh);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace modelrepair

#endif // MODELREPAIR_HAVE_VULKAN

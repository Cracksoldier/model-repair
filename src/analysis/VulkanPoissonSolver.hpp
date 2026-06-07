#pragma once

#include <functional>
#include <vector>

namespace modelrepair
{

// GPU Jacobi-preconditioned CG solver for the 2D Poisson system
// that arises in normal-map-to-displacement-map reconstruction.
// Uses Vulkan compute; only compiled when MODELREPAIR_HAVE_VULKAN is defined.
//
// Usage:
//   VulkanPoissonSolver solver(W, H, b);
//   if (!solver.valid()) { /* fall back to CPU */ }
//   solver.run(max_iter, 25, on_iteration_callback);
//   auto h = solver.download();
class VulkanPoissonSolver
{
public:
    // b must have exactly W*H elements; b[0] must be 0 (Dirichlet BC).
    VulkanPoissonSolver(int W, int H, const std::vector<float>& b);
    ~VulkanPoissonSolver();

    VulkanPoissonSolver(const VulkanPoissonSolver&) = delete;
    VulkanPoissonSolver& operator=(const VulkanPoissonSolver&) = delete;

    bool valid()          const;
    bool converged()      const;
    int  iterations_done() const;

    // Run up to n_iterations CG steps in batches of batch_size.
    // on_iteration(iter) is called after each batch; return false to cancel.
    void run(int n_iterations, int batch_size = 25,
             std::function<bool(int)> on_iteration = nullptr);

    // Download the solution vector (W*H floats) from the GPU.
    std::vector<float> download() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace modelrepair

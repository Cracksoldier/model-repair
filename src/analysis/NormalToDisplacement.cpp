// stb_image — header-only image loader; define implementation in this TU only.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#include "modelrepair/NormalToDisplacement.hpp"

#include <Eigen/IterativeLinearSolvers>
#include <Eigen/Sparse>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace modelrepair
{

namespace
{

// Separable box blur on a float image (values in [0,1]).
void box_blur(std::vector<float>& img, int W, int H, float radius)
{
    const int r = static_cast<int>(radius);
    if (r < 1) return;

    std::vector<float> tmp(static_cast<std::size_t>(W * H));

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            float sum = 0.f;
            int   cnt = 0;
            for (int dx = -r; dx <= r; ++dx)
            {
                const int nx = std::clamp(x + dx, 0, W - 1);
                sum += img[static_cast<std::size_t>(y * W + nx)];
                ++cnt;
            }
            tmp[static_cast<std::size_t>(y * W + x)] = sum / static_cast<float>(cnt);
        }

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            float sum = 0.f;
            int   cnt = 0;
            for (int dy = -r; dy <= r; ++dy)
            {
                const int ny = std::clamp(y + dy, 0, H - 1);
                sum += tmp[static_cast<std::size_t>(ny * W + x)];
                ++cnt;
            }
            img[static_cast<std::size_t>(y * W + x)] = sum / static_cast<float>(cnt);
        }
}

} // namespace

NormalToDisplacementResult normal_to_displacement(
    const std::string& normal_map_path,
    const NormalToDisplacementSettings& settings)
{
    const auto t0 = std::chrono::steady_clock::now();

    // ── 1. Load image ────────────────────────────────────────────────────────
    int W = 0, H = 0, channels = 0;
    unsigned char* raw = stbi_load(normal_map_path.c_str(), &W, &H, &channels, 3);
    if (!raw)
        throw std::runtime_error(
            std::string("NormalToDisplacement: cannot load '") +
            normal_map_path + "': " + stbi_failure_reason());
    struct StbiDeleter { void operator()(unsigned char* p) const { stbi_image_free(p); } };
    std::unique_ptr<unsigned char, StbiDeleter> raw_guard(raw);

    const long long N_ll = static_cast<long long>(W) * H;
    if (N_ll > 8'000'000LL)
        throw std::runtime_error(
            "NormalToDisplacement: image too large (" + std::to_string(W) + "x" +
            std::to_string(H) + " = " + std::to_string(N_ll) +
            " pixels). Downsample to <=2828x2828 first.");
    const int N = static_cast<int>(N_ll);

    // ── 2. Decode normals → gradient field ──────────────────────────────────
    // gx ≈ dh/dx,  gy ≈ dh/dy,  derived from nz via  g = -n_tan / max(nz, ε)
    constexpr float eps     = 1e-4f;
    constexpr float max_grad = 20.0f;
    const float gs = settings.gradient_strength;

    std::vector<float> gx(static_cast<std::size_t>(N));
    std::vector<float> gy(static_cast<std::size_t>(N));

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const unsigned char* p = raw + static_cast<std::size_t>(y * W + x) * 3;
            float nx_v = p[0] / 255.f * 2.f - 1.f;
            float ny_v = p[1] / 255.f * 2.f - 1.f;
            float nz_v = p[2] / 255.f * 2.f - 1.f;

            if (settings.flip_green) ny_v = -ny_v;

            const float len = std::sqrt(nx_v * nx_v + ny_v * ny_v + nz_v * nz_v);
            if (len > 1e-8f) { nx_v /= len; ny_v /= len; nz_v /= len; }

            const float inv_nz = 1.f / std::max(nz_v, eps);
            const std::size_t idx = static_cast<std::size_t>(y * W + x);
            gx[idx] = std::clamp(-nx_v * inv_nz * gs, -max_grad, max_grad);
            gy[idx] = std::clamp(-ny_v * inv_nz * gs, -max_grad, max_grad);
        }

    // ── 3. Compute divergence of gradient field ──────────────────────────────
    // div(x,y) = (gx[x,y] - gx[x-1,y]) + (gy[x,y] - gy[x,y-1])
    // We negate to form the positive-definite system (-∇²)h = -div.
    Eigen::VectorXf b(N);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const std::size_t idx = static_cast<std::size_t>(y * W + x);
            const float dgx = gx[idx] - (x > 0 ? gx[static_cast<std::size_t>(y * W + x - 1)] : 0.f);
            const float dgy = gy[idx] - (y > 0 ? gy[static_cast<std::size_t>((y - 1) * W + x)] : 0.f);
            b[static_cast<Eigen::Index>(idx)] = -(dgx + dgy);
        }

    // ── 4. Build sparse negated Laplacian (positive semi-definite) ───────────
    // Neumann BC: truncated stencil at boundaries (n_neighbors determines diagonal).
    // Dirichlet at pixel 0: h[0]=0, applied symmetrically (zero out row 0 and col 0).
    using SpMat   = Eigen::SparseMatrix<float>;
    using Triplet = Eigen::Triplet<float>;

    std::vector<Triplet> trips;
    trips.reserve(static_cast<std::size_t>(N) * 5);

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const int idx = y * W + x;
            if (idx == 0) continue;   // handled separately for Dirichlet

            int n_neighbors = 0;
            auto add_off = [&](int j) {
                ++n_neighbors;        // always count this edge — preserves true Laplacian degree
                if (j == 0) return;   // Dirichlet BC: zero column entry only, diagonal stays
                trips.emplace_back(idx, j, -1.f);
            };

            if (x > 0)     add_off(idx - 1);      else ++n_neighbors;
            if (x < W - 1) add_off(idx + 1);      else ++n_neighbors;
            if (y > 0)     add_off(idx - W);       else ++n_neighbors;
            if (y < H - 1) add_off(idx + W);       else ++n_neighbors;

            trips.emplace_back(idx, idx, static_cast<float>(n_neighbors));
        }

    // Row 0 = Dirichlet identity
    trips.emplace_back(0, 0, 1.f);
    b[0] = 0.f;

    SpMat A(N, N);
    A.setFromTriplets(trips.begin(), trips.end());

    // ── 5. Solve with ConjugateGradient ──────────────────────────────────────
    Eigen::ConjugateGradient<SpMat, Eigen::Lower | Eigen::Upper,
                             Eigen::DiagonalPreconditioner<float>> cg;
    cg.setMaxIterations(settings.solver_max_iter);
    cg.setTolerance(1e-5f);
    cg.compute(A);
    const Eigen::VectorXf h_vec = cg.solve(b);

    // ── 6. Post-processing ────────────────────────────────────────────────────
    std::vector<float> height(h_vec.data(), h_vec.data() + N);

    if (settings.blur_radius >= 0.5f)
        box_blur(height, W, H, settings.blur_radius);

    if (settings.normalize_output)
    {
        const float mn = *std::min_element(height.begin(), height.end());
        const float mx = *std::max_element(height.begin(), height.end());
        const float range = mx - mn;
        if (range > 1e-8f)
            for (float& v : height)
                v = (v - mn) / range;
        else
            std::fill(height.begin(), height.end(), 0.f);
    }

    if (std::abs(settings.contrast - 1.f) > 1e-4f)
        for (float& v : height)
            v = std::clamp((v - 0.5f) * settings.contrast + 0.5f, 0.f, 1.f);

    if (settings.invert_height)
        for (float& v : height)
            v = 1.f - v;

    const float ms = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    return {std::move(height), W, H, ms};
}

} // namespace modelrepair

#include "modelrepair/Smooth.hpp"

#include <CGAL/Polygon_mesh_processing/compute_normal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

#ifdef MODELREPAIR_HAVE_TBB
#  include <execution>
#endif

#ifdef MODELREPAIR_HAVE_VULKAN
#  include <vulkan/vulkan.h>
#  include "VulkanSmoothing.hpp"
#endif

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

bool smooth_vulkan_available()
{
#ifdef MODELREPAIR_HAVE_VULKAN
    static const bool avail = []() -> bool {
        VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        ai.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &ai;
        VkInstance inst;
        if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS) return false;
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(inst, &count, nullptr);
        vkDestroyInstance(inst, nullptr);
        return count > 0;
    }();
    return avail;
#else
    return false;
#endif
}

SmoothResult smooth(Mesh& mesh, unsigned int iterations, double crease_angle,
                    std::function<void(unsigned int)> on_iteration, bool use_vulkan)
{
    auto t0 = std::chrono::steady_clock::now();

    SurfMesh& M = mesh.cgal();

    // Capture volume before smoothing so we can restore it afterwards.
    auto vol_before = mesh.volume(); // nullopt if mesh is not closed

    bool gpu_done = false;
#ifdef MODELREPAIR_HAVE_VULKAN
    if (use_vulkan && smooth_vulkan_available()) {
        VulkanSmoothing vs(mesh, crease_angle);
        if (vs.valid()) {
            vs.run(iterations, on_iteration);
            vs.download(mesh);
            gpu_done = true;
        }
        // If Vulkan init failed unexpectedly, fall through to CPU path.
    }
#else
    (void)use_vulkan;
#endif

    if (!gpu_done) {
        using V3 = Kernel::Vector_3;

        constexpr double lambda = 0.5;
        const double cos_thresh = std::cos(crease_angle * std::numbers::pi / 180.0);

        std::vector<Point3> buf(M.num_vertices());

        // Face-normal property map reused each iteration.
        auto [fnormals, created] =
            M.add_property_map<SurfMesh::Face_index, V3>("f:sn_smooth");
        (void)created;

        // Cotangent-weighted feature-preserving Laplacian step.
        // Vertex list built once per smooth() call; reused across iterations.
        // Collecting into a contiguous vector gives par_unseq a random-access range,
        // which all implementations handle well regardless of CGAL iterator category.
        std::vector<SurfMesh::Vertex_index> vlist(vertices(M).begin(), vertices(M).end());

        auto one_vertex = [&](SurfMesh::Vertex_index v) {
            const std::size_t i = v.idx();
            buf[i] = M.point(v);
            if (M.is_border(v)) return;

            double w_sum = 0.0;
            V3     w_pos = CGAL::NULL_VECTOR;

            for (auto h : halfedges_around_target(v, M)) {
                auto f1 = face(h, M);
                auto f2 = face(opposite(h, M), M);

                // Soft crease falloff: weight scales linearly from 1.0 (flat edge,
                // dot = 1) down to 0.0 (edge at the crease threshold, dot = cos_thresh).
                // Edges beyond the threshold get 0 weight; edges near it get partial
                // weight, so mild-curvature features erode more slowly than flat areas.
                double crease_w = 1.0;
                if (f1 != SurfMesh::null_face() && f2 != SurfMesh::null_face()) {
                    const V3& n1 = fnormals[f1];
                    const V3& n2 = fnormals[f2];
                    double len = std::sqrt(CGAL::to_double(n1 * n1))
                               * std::sqrt(CGAL::to_double(n2 * n2));
                    double dot_norm = (len < 1e-12) ? -1.0
                                      : CGAL::to_double(n1 * n2) / len;
                    double denom    = std::max(1.0 - cos_thresh, 1e-6);
                    crease_w = std::max(0.0,
                        std::min(1.0, (dot_norm - cos_thresh) / denom));
                }
                if (crease_w < 1e-6) continue;

                auto s = source(h, M);
                double w = 0.0;

                // cot of angle at third vertex of face f1 (triangle: s → v → w1)
                if (f1 != SurfMesh::null_face()) {
                    auto w1 = target(next(h, M), M);
                    auto a  = M.point(s) - M.point(w1);
                    auto b  = M.point(v) - M.point(w1);
                    auto cx = CGAL::cross_product(a, b);
                    double cl = std::sqrt(CGAL::to_double(cx * cx));
                    if (cl > 1e-12)
                        w += CGAL::to_double(a * b) / cl;
                }

                // cot of angle at third vertex of face f2 (triangle: v → s → w2)
                if (f2 != SurfMesh::null_face()) {
                    auto w2 = target(next(opposite(h, M), M), M);
                    auto a  = M.point(v) - M.point(w2);
                    auto b  = M.point(s) - M.point(w2);
                    auto cx = CGAL::cross_product(a, b);
                    double cl = std::sqrt(CGAL::to_double(cx * cx));
                    if (cl > 1e-12)
                        w += CGAL::to_double(a * b) / cl;
                }

                w = std::max(0.0, w) * crease_w; // clamp negative, apply crease falloff
                w_sum += w;
                w_pos = w_pos + w * (M.point(s) - CGAL::ORIGIN);
            }

            if (w_sum > 1e-12) {
                Point3 avg = CGAL::ORIGIN + w_pos / w_sum;
                buf[i] = M.point(v) + lambda * (avg - M.point(v));
            }
        };

        auto laplacian_step = [&]() {
            PMP::compute_face_normals(M, fnormals);

            // Each vertex reads only current neighbour positions (M.point) and the
            // pre-computed face normals, both read-only during this phase. Each vertex
            // writes exclusively to buf[v.idx()], so there are no data races.
#ifdef MODELREPAIR_HAVE_TBB
            std::for_each(std::execution::par_unseq,
                          vlist.begin(), vlist.end(), one_vertex);
#else
            std::for_each(vlist.begin(), vlist.end(), one_vertex);
#endif

            // Apply: write new positions back. Each vertex touches a distinct element
            // of the mesh's internal point array — safe to parallelise.
#ifdef MODELREPAIR_HAVE_TBB
            std::for_each(std::execution::par_unseq, vlist.begin(), vlist.end(),
                          [&](SurfMesh::Vertex_index v) { M.point(v) = buf[v.idx()]; });
#else
            for (auto v : vlist)
                M.point(v) = buf[v.idx()];
#endif
        };

        for (unsigned int i = 0; i < iterations; ++i) {
            laplacian_step();
            if (on_iteration) on_iteration(i + 1);
        }

        M.remove_property_map(fnormals);
    }

    // Restore original volume via uniform scaling around the centroid.
    // Laplacian smoothing systematically shrinks the mesh; this corrects it.
    if (vol_before && *vol_before > 1e-10) {
        auto vol_after = mesh.volume();
        if (vol_after && *vol_after > 1e-10) {
            using V3 = Kernel::Vector_3;
            double scale = std::cbrt(*vol_before / *vol_after);
            V3 centroid = CGAL::NULL_VECTOR;
            for (auto v : vertices(M))
                centroid = centroid + (M.point(v) - CGAL::ORIGIN);
            centroid = centroid / static_cast<double>(M.num_vertices());
            Point3 c = CGAL::ORIGIN + centroid;
            for (auto v : vertices(M))
                M.point(v) = c + scale * (M.point(v) - c);
        }
    }

    auto t1 = std::chrono::steady_clock::now();

    SmoothResult r;
    r.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

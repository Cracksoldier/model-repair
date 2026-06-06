// stb_image — header-only image loader; define implementation in this TU only.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

#include "modelrepair/NormalMapDisplace.hpp"
#include "modelrepair/Subdivide.hpp"

#include <CGAL/Polygon_mesh_processing/compute_normal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

NormalMapDisplaceResult displace_from_normal_map(Mesh& mesh,
                                                  const NormalMapDisplaceParams& params)
{
    if (!mesh.has_uv())
        throw std::runtime_error(
            "Mesh has no UV coordinates. "
            "Load an OBJ file that exports UV data to use Normal Map Displacement.");

    auto t0 = std::chrono::steady_clock::now();

    NormalMapDisplaceResult result;
    result.faces_before = static_cast<int>(mesh.num_faces());

    // Subdivide first so there are enough vertices to capture normal-map detail.
    // subdivide() propagates the "v:uv" property map to new vertices automatically.
    if (params.pre_subdivisions > 0)
        modelrepair::subdivide(mesh, static_cast<unsigned int>(params.pre_subdivisions),
                               SubdivisionMethod::Loop);

    auto& sm = mesh.cgal();

    // Load normal map image (RGB, 3 channels forced).
    int img_w = 0, img_h = 0, img_ch = 0;
    unsigned char* img_data = stbi_load(params.normal_map_path.c_str(),
                                        &img_w, &img_h, &img_ch, 3);
    if (!img_data)
        throw std::runtime_error(std::string("Cannot load normal map '")
                                 + params.normal_map_path + "': "
                                 + stbi_failure_reason());

    // Re-fetch UV map after subdivision.
    auto uv_opt = sm.property_map<SurfMesh::Vertex_index, UV2>("v:uv");
    if (!uv_opt.has_value())
    {
        stbi_image_free(img_data);
        throw std::runtime_error("UV property map was lost during subdivision.");
    }
    auto& uv_map = *uv_opt;

    // Compute per-vertex normals (used as displacement direction).
    using VNMap = SurfMesh::Property_map<SurfMesh::Vertex_index, Kernel::Vector_3>;
    auto [vn_map, vn_ok] = sm.add_property_map<SurfMesh::Vertex_index, Kernel::Vector_3>(
        "v:nml_tmp", Kernel::Vector_3(0, 0, 1));
    PMP::compute_vertex_normals(sm, vn_map);

    // Displace each vertex along its normal based on the pseudo-height sampled
    // from the normal map's blue/Z channel: flat regions (nz≈1) → no displacement;
    // tilted regions (nz≈0) → full displacement_strength push outward.
    for (auto v : sm.vertices())
    {
        const UV2& uv = uv_map[v];
        float u  = uv[0];
        float fv = uv[1];

        // Wrap UVs to [0, 1).
        u  -= std::floor(u);
        fv -= std::floor(fv);

        // Normal-map images have Y=0 at the top; UV Y=0 is at the bottom.
        if (params.flip_y_uv)
            fv = 1.0f - fv;

        // Nearest-neighbour sample (sufficient for prototype baking).
        int px = std::clamp(static_cast<int>(u  * (img_w - 1)), 0, img_w - 1);
        int py = std::clamp(static_cast<int>(fv * (img_h - 1)), 0, img_h - 1);

        const unsigned char* pixel = img_data + (py * img_w + px) * 3;
        float r = pixel[0] / 255.0f;
        float g = pixel[1] / 255.0f;
        float b = pixel[2] / 255.0f;

        if (params.flip_green)
            g = 1.0f - g;

        // Decode tangent-space normal from [0,1] to [-1,1].
        // For pseudo-height, we only use the Z/blue component.
        const float nz          = b * 2.0f - 1.0f;
        const float pseudo_height = 1.0f - nz;         // flat→0, steep→2
        const float displacement  = pseudo_height * params.displacement_strength;

        // Move vertex along its geometric normal.
        const auto& vn = vn_map[v];
        const auto& pt = sm.point(v);
        sm.point(v) = Point3(pt.x() + vn.x() * displacement,
                              pt.y() + vn.y() * displacement,
                              pt.z() + vn.z() * displacement);
    }

    sm.remove_property_map(vn_map);
    stbi_image_free(img_data);

    result.faces_after = static_cast<int>(mesh.num_faces());
    auto t1 = std::chrono::steady_clock::now();
    result.duration_ms = static_cast<float>(
        std::chrono::duration<double, std::milli>(t1 - t0).count());

    return result;
}

} // namespace modelrepair

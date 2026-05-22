// tinygltf is used header-only: define TINYGLTF_IMPLEMENTATION in exactly this TU.
// Disable stb_image — we only need geometry, not texture loading.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include "modelrepair/io/GlbIO.hpp"

#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair::io
{

namespace
{

void collect_positions(const tinygltf::Model& model, int acc_idx,
                       std::vector<Point3>& out)
{
    const auto& acc = model.accessors[acc_idx];
    const auto& bv  = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];

    if (acc.type != TINYGLTF_TYPE_VEC3 || acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        throw std::runtime_error("GLB: POSITION accessor must be VEC3 FLOAT");

    const std::size_t stride = bv.byteStride ? bv.byteStride : 12;
    const std::size_t base   = bv.byteOffset + acc.byteOffset;

    out.reserve(out.size() + acc.count);
    for (std::size_t i = 0; i < acc.count; ++i)
    {
        float xyz[3];
        std::memcpy(xyz, buf.data.data() + base + i * stride, 12);
        out.emplace_back(xyz[0], xyz[1], xyz[2]);
    }
}

void collect_triangles(const tinygltf::Model& model, int acc_idx,
                       std::size_t base_vertex,
                       std::vector<std::vector<std::size_t>>& out)
{
    const auto& acc = model.accessors[acc_idx];
    const auto& bv  = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];

    if (acc.type != TINYGLTF_TYPE_SCALAR)
        throw std::runtime_error("GLB: INDICES accessor must be SCALAR");
    if (acc.count % 3 != 0)
        throw std::runtime_error("GLB: INDICES count is not a multiple of 3");

    const std::size_t base = bv.byteOffset + acc.byteOffset;

    auto get_idx = [&](std::size_t i) -> std::size_t
    {
        switch (acc.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const std::size_t stride = bv.byteStride ? bv.byteStride : 1;
            return static_cast<std::size_t>(buf.data[base + i * stride]);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const std::size_t stride = bv.byteStride ? bv.byteStride : 2;
            uint16_t v;
            std::memcpy(&v, buf.data.data() + base + i * stride, 2);
            return static_cast<std::size_t>(v);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            const std::size_t stride = bv.byteStride ? bv.byteStride : 4;
            uint32_t v;
            std::memcpy(&v, buf.data.data() + base + i * stride, 4);
            return static_cast<std::size_t>(v);
        }
        default:
            throw std::runtime_error("GLB: unsupported index component type");
        }
    };

    const std::size_t tri_count = acc.count / 3;
    out.reserve(out.size() + tri_count);
    for (std::size_t t = 0; t < tri_count; ++t)
        out.push_back({base_vertex + get_idx(t * 3),
                       base_vertex + get_idx(t * 3 + 1),
                       base_vertex + get_idx(t * 3 + 2)});
}

} // namespace

Mesh read_glb(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        throw std::runtime_error("File not found: " + path.string());

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    const bool ok = (ext == ".glb")
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path.string())
        : loader.LoadASCIIFromFile(&model, &err, &warn, path.string());

    if (!ok)
        throw std::runtime_error("Failed to load '" + path.string() + "': " + err);

    std::vector<Point3> points;
    std::vector<std::vector<std::size_t>> polygons;

    for (const auto& mesh : model.meshes)
    {
        for (const auto& prim : mesh.primitives)
        {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES)
                continue;
            if (prim.indices < 0)
                continue; // non-indexed geometry not supported

            const auto pos_it = prim.attributes.find("POSITION");
            if (pos_it == prim.attributes.end())
                continue;

            const std::size_t base = points.size();
            collect_positions(model, pos_it->second, points);
            collect_triangles(model, prim.indices, base, polygons);
        }
    }

    if (polygons.empty())
        throw std::runtime_error("No triangle geometry found in '" + path.string() + "'");

    PMP::orient_polygon_soup(points, polygons);
    Mesh mesh;
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh.cgal());
    return mesh;
}

void write_glb(const Mesh& mesh, const std::filesystem::path& path)
{
    const auto& sm = mesh.cgal();

    // Build packed position data and vertex index map.
    std::vector<float> pos_data;
    pos_data.reserve(sm.number_of_vertices() * 3);
    std::unordered_map<SurfMesh::Vertex_index, uint32_t> vmap;
    vmap.reserve(sm.number_of_vertices());

    float min_xyz[3] = { std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max() };
    float max_xyz[3] = { std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest() };

    uint32_t vi = 0;
    for (const auto v : sm.vertices())
    {
        const auto& p = sm.point(v);
        const float x = static_cast<float>(CGAL::to_double(p.x()));
        const float y = static_cast<float>(CGAL::to_double(p.y()));
        const float z = static_cast<float>(CGAL::to_double(p.z()));
        pos_data.push_back(x);
        pos_data.push_back(y);
        pos_data.push_back(z);
        min_xyz[0] = std::min(min_xyz[0], x);
        min_xyz[1] = std::min(min_xyz[1], y);
        min_xyz[2] = std::min(min_xyz[2], z);
        max_xyz[0] = std::max(max_xyz[0], x);
        max_xyz[1] = std::max(max_xyz[1], y);
        max_xyz[2] = std::max(max_xyz[2], z);
        vmap[v] = vi++;
    }

    // Build index buffer.
    std::vector<uint32_t> idx_data;
    idx_data.reserve(sm.number_of_faces() * 3);
    for (const auto f : sm.faces())
    {
        auto he = sm.halfedge(f);
        idx_data.push_back(vmap.at(sm.source(he)));
        idx_data.push_back(vmap.at(sm.target(he)));
        idx_data.push_back(vmap.at(sm.target(sm.next(he))));
    }

    // Pack into a single binary buffer: positions then indices.
    const std::size_t pos_bytes = pos_data.size() * sizeof(float);
    const std::size_t idx_bytes = idx_data.size() * sizeof(uint32_t);
    std::vector<unsigned char> buf_data(pos_bytes + idx_bytes);
    std::memcpy(buf_data.data(),             pos_data.data(), pos_bytes);
    std::memcpy(buf_data.data() + pos_bytes, idx_data.data(), idx_bytes);

    tinygltf::Model gltf;
    gltf.asset.version   = "2.0";
    gltf.asset.generator = "model-repair";

    tinygltf::Buffer buf;
    buf.data = std::move(buf_data);
    gltf.buffers.push_back(std::move(buf));

    tinygltf::BufferView bv_pos;
    bv_pos.buffer     = 0;
    bv_pos.byteOffset = 0;
    bv_pos.byteLength = pos_bytes;
    bv_pos.target     = TINYGLTF_TARGET_ARRAY_BUFFER;
    gltf.bufferViews.push_back(std::move(bv_pos));

    tinygltf::BufferView bv_idx;
    bv_idx.buffer     = 0;
    bv_idx.byteOffset = pos_bytes;
    bv_idx.byteLength = idx_bytes;
    bv_idx.target     = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    gltf.bufferViews.push_back(std::move(bv_idx));

    tinygltf::Accessor acc_pos;
    acc_pos.bufferView    = 0;
    acc_pos.byteOffset    = 0;
    acc_pos.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    acc_pos.count         = sm.number_of_vertices();
    acc_pos.type          = TINYGLTF_TYPE_VEC3;
    acc_pos.minValues     = {min_xyz[0], min_xyz[1], min_xyz[2]};
    acc_pos.maxValues     = {max_xyz[0], max_xyz[1], max_xyz[2]};
    gltf.accessors.push_back(std::move(acc_pos));

    tinygltf::Accessor acc_idx;
    acc_idx.bufferView    = 1;
    acc_idx.byteOffset    = 0;
    acc_idx.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    acc_idx.count         = idx_data.size();
    acc_idx.type          = TINYGLTF_TYPE_SCALAR;
    gltf.accessors.push_back(std::move(acc_idx));

    tinygltf::Primitive prim;
    prim.attributes["POSITION"] = 0;
    prim.indices                 = 1;
    prim.mode                    = TINYGLTF_MODE_TRIANGLES;

    tinygltf::Mesh gmesh;
    gmesh.primitives.push_back(std::move(prim));
    gltf.meshes.push_back(std::move(gmesh));

    tinygltf::Node node;
    node.mesh = 0;
    gltf.nodes.push_back(std::move(node));

    tinygltf::Scene scene;
    scene.nodes = {0};
    gltf.scenes.push_back(std::move(scene));
    gltf.defaultScene = 0;

    tinygltf::TinyGLTF writer;
    if (!writer.WriteGltfSceneToFile(&gltf, path.string(),
                                     /*embedImages=*/true,
                                     /*embedBuffers=*/true,
                                     /*prettyPrint=*/false,
                                     /*writeBinary=*/true))
        throw std::runtime_error("Failed to write GLB: " + path.string());
}

} // namespace modelrepair::io

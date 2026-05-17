#include "modelrepair/io/ThreeMFIO.hpp"

#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <lib3mf_implicit.hpp>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair::io
{

Mesh read_3mf(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        throw std::runtime_error("File not found: " + path.string());

    auto wrapper  = Lib3MF::CWrapper::loadLibrary();
    auto model    = wrapper->CreateModel();
    auto reader   = model->QueryReader("3mf");
    reader->ReadFromFile(path.string());

    auto it = model->GetMeshObjects();
    if (!it->MoveNext())
        throw std::runtime_error("3MF file contains no mesh objects: " + path.string());

    auto obj = it->GetCurrentMeshObject();

    // Vertices
    std::vector<Point3> points;
    Lib3MF_uint64 vertex_count = obj->GetVertexCount();
    points.reserve(vertex_count);
    for (Lib3MF_uint64 i = 0; i < vertex_count; ++i)
    {
        auto v = obj->GetVertex(i);
        points.emplace_back(v.m_Coordinates[0],
                            v.m_Coordinates[1],
                            v.m_Coordinates[2]);
    }

    // Triangles
    std::vector<std::vector<std::size_t>> polygons;
    Lib3MF_uint64 tri_count = obj->GetTriangleCount();
    polygons.reserve(tri_count);
    for (Lib3MF_uint64 i = 0; i < tri_count; ++i)
    {
        auto t = obj->GetTriangle(i);
        polygons.push_back({static_cast<std::size_t>(t.m_Indices[0]),
                            static_cast<std::size_t>(t.m_Indices[1]),
                            static_cast<std::size_t>(t.m_Indices[2])});
    }

    PMP::orient_polygon_soup(points, polygons);

    Mesh mesh;
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh.cgal());
    return mesh;
}

void write_3mf(const Mesh& mesh, const std::filesystem::path& path)
{
    auto wrapper = Lib3MF::CWrapper::loadLibrary();
    auto model   = wrapper->CreateModel();
    auto obj     = model->AddMeshObject();
    obj->SetName("repaired");

    const auto& sm = mesh.cgal();

    // Build vertex index map.
    std::unordered_map<SurfMesh::Vertex_index, Lib3MF_uint32> vmap;
    for (auto v : sm.vertices())
    {
        const auto& p = sm.point(v);
        Lib3MF::sPosition pos;
        pos.m_Coordinates[0] = static_cast<float>(CGAL::to_double(p.x()));
        pos.m_Coordinates[1] = static_cast<float>(CGAL::to_double(p.y()));
        pos.m_Coordinates[2] = static_cast<float>(CGAL::to_double(p.z()));
        vmap[v] = obj->AddVertex(pos);
    }

    for (auto face : sm.faces())
    {
        auto he = sm.halfedge(face);
        Lib3MF::sTriangle tri;
        tri.m_Indices[0] = vmap.at(sm.source(he));
        tri.m_Indices[1] = vmap.at(sm.target(he));
        tri.m_Indices[2] = vmap.at(sm.target(sm.next(he)));
        obj->AddTriangle(tri);
    }

    model->AddBuildItem(obj.get(), wrapper->GetIdentityTransform());

    auto writer = model->QueryWriter("3mf");
    writer->WriteToFile(path.string());
}

} // namespace modelrepair::io

#include "modelrepair/io/PlyIO.hpp"

#include <CGAL/IO/PLY.h>
#include <CGAL/IO/Color.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair::io
{

Mesh read_ply(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        throw std::runtime_error("File not found: " + path.string());

    std::ifstream is(path, std::ios::binary);
    if (!is)
        throw std::runtime_error("Cannot open file: " + path.string());

    std::vector<Point3> points;
    std::vector<std::vector<std::size_t>> polygons;
    std::vector<CGAL::IO::Color> fcolors, vcolors;

    if (!CGAL::IO::read_PLY(is, points, polygons, fcolors, vcolors))
        throw std::runtime_error("Failed to parse PLY: " + path.string());

    if (polygons.empty())
        throw std::runtime_error("No triangle geometry found in '" + path.string() + "'");

    // Snapshot before orient_polygon_soup, which may append points for non-manifold fans.
    const std::size_t n_vcolors = vcolors.size();
    const std::size_t n_points_before_orient = points.size();
    PMP::orient_polygon_soup(points, polygons);

    Mesh mesh;
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh.cgal());

    // Attach vertex colors when the PLY file contained them.
    // After polygon_soup_to_polygon_mesh, Vertex_index(i) corresponds to points[i].
    if (n_vcolors > 0 && n_vcolors == n_points_before_orient)
    {
        auto [cmap, ok] = mesh.cgal().add_property_map<SurfMesh::Vertex_index, CGAL::IO::Color>(
            "v:color", CGAL::IO::Color(128, 128, 128));
        if (ok)
        {
            for (std::size_t i = 0; i < n_vcolors; ++i)
                cmap[SurfMesh::Vertex_index(static_cast<SurfMesh::size_type>(i))] = vcolors[i];
        }
    }

    return mesh;
}

void write_ply(const Mesh& mesh, const std::filesystem::path& path)
{
    const auto& sm = mesh.cgal();

    if (sm.number_of_faces() == 0)
        throw std::runtime_error("Cannot write empty mesh as PLY");

    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("Cannot write file: " + path.string());

    // Check for vertex colors.
    auto cmap_opt = sm.property_map<SurfMesh::Vertex_index, CGAL::IO::Color>("v:color");
    const bool has_colors = cmap_opt.has_value();

    // Build stable vertex index map.
    std::vector<SurfMesh::Vertex_index> vorder;
    vorder.reserve(sm.number_of_vertices());
    std::vector<std::uint32_t> vidx(sm.num_vertices(), 0);
    std::uint32_t vi = 0;
    for (const auto v : sm.vertices())
    {
        vidx[static_cast<std::size_t>(v)] = vi++;
        vorder.push_back(v);
    }

    // Write PLY header (text).
    out << "ply\n"
        << "format binary_little_endian 1.0\n"
        << "comment model-repair\n"
        << "element vertex " << sm.number_of_vertices() << "\n"
        << "property float x\n"
        << "property float y\n"
        << "property float z\n";
    if (has_colors)
    {
        out << "property uchar red\n"
            << "property uchar green\n"
            << "property uchar blue\n";
    }
    out << "element face " << sm.number_of_faces() << "\n"
        << "property list uchar uint vertex_indices\n"
        << "end_header\n";

    // Write vertex data (binary LE).
    for (const auto v : vorder)
    {
        const auto& p = sm.point(v);
        const float x = static_cast<float>(CGAL::to_double(p.x()));
        const float y = static_cast<float>(CGAL::to_double(p.y()));
        const float z = static_cast<float>(CGAL::to_double(p.z()));
        out.write(reinterpret_cast<const char*>(&x), 4);
        out.write(reinterpret_cast<const char*>(&y), 4);
        out.write(reinterpret_cast<const char*>(&z), 4);
        if (has_colors)
        {
            const auto& c = cmap_opt.value()[v];
            out.put(static_cast<char>(c.red()));
            out.put(static_cast<char>(c.green()));
            out.put(static_cast<char>(c.blue()));
        }
    }

    // Write face data (binary LE): each face is [count=3, i0, i1, i2].
    const std::uint8_t three = 3;
    for (const auto f : sm.faces())
    {
        auto he = sm.halfedge(f);
        const std::uint32_t i0 = vidx[static_cast<std::size_t>(sm.source(he))];
        const std::uint32_t i1 = vidx[static_cast<std::size_t>(sm.target(he))];
        const std::uint32_t i2 = vidx[static_cast<std::size_t>(sm.target(sm.next(he)))];
        out.write(reinterpret_cast<const char*>(&three), 1);
        out.write(reinterpret_cast<const char*>(&i0), 4);
        out.write(reinterpret_cast<const char*>(&i1), 4);
        out.write(reinterpret_cast<const char*>(&i2), 4);
    }
}

} // namespace modelrepair::io

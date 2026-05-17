#include "modelrepair/io/StlIO.hpp"

#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair::io
{

namespace
{

// Returns true if the file is binary STL (heuristic: check header and triangle count).
bool is_binary_stl(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path.string());

    char header[80];
    f.read(header, 80);
    if (f.gcount() < 80)
        return false;

    // ASCII STL starts with "solid"
    if (std::strncmp(header, "solid", 5) == 0)
    {
        // Could still be binary if the header coincidentally starts with "solid".
        // Check: binary STL has uint32 triangle count followed by 50-byte records.
        uint32_t count = 0;
        f.read(reinterpret_cast<char*>(&count), 4);
        auto expected_size = static_cast<std::uintmax_t>(80 + 4 + count * 50);
        auto actual_size   = std::filesystem::file_size(path);
        return actual_size == expected_size;
    }
    return true;
}

Mesh read_binary_stl(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path.string());

    char header[80];
    f.read(header, 80);

    uint32_t num_triangles = 0;
    f.read(reinterpret_cast<char*>(&num_triangles), 4);

    std::vector<Point3> points;
    std::vector<std::vector<std::size_t>> polygons;
    points.reserve(num_triangles * 3);
    polygons.reserve(num_triangles);

    for (uint32_t i = 0; i < num_triangles; ++i)
    {
        float normal[3], verts[9];
        uint16_t attr;
        f.read(reinterpret_cast<char*>(normal), 12);
        f.read(reinterpret_cast<char*>(verts),  36);
        f.read(reinterpret_cast<char*>(&attr),   2);

        std::size_t base = points.size();
        for (int j = 0; j < 3; ++j)
            points.emplace_back(verts[j*3], verts[j*3+1], verts[j*3+2]);
        polygons.push_back({base, base+1, base+2});
    }

    PMP::orient_polygon_soup(points, polygons);

    Mesh mesh;
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh.cgal());
    return mesh;
}

Mesh read_ascii_stl(const std::filesystem::path& path)
{
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path.string());

    std::vector<Point3> points;
    std::vector<std::vector<std::size_t>> polygons;

    std::string token;
    while (f >> token)
    {
        if (token == "facet")
        {
            // skip "normal nx ny nz"
            std::string word; double nx, ny, nz;
            f >> word >> nx >> ny >> nz;
            // "outer loop"
            f >> word >> word;

            std::size_t base = points.size();
            for (int v = 0; v < 3; ++v)
            {
                double x, y, z;
                f >> word >> x >> y >> z; // "vertex x y z"
                points.emplace_back(x, y, z);
            }
            polygons.push_back({base, base+1, base+2});
            // "endloop", "endfacet"
            f >> word >> word;
        }
    }

    PMP::orient_polygon_soup(points, polygons);

    Mesh mesh;
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh.cgal());
    return mesh;
}

} // namespace

Mesh read_stl(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        throw std::runtime_error("File not found: " + path.string());

    if (is_binary_stl(path))
        return read_binary_stl(path);
    return read_ascii_stl(path);
}

void write_stl(const Mesh& mesh, const std::filesystem::path& path, bool binary)
{
    if (binary)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("Cannot write file: " + path.string());

        // 80-byte header
        char header[80] = {};
        std::strncpy(header, "Binary STL written by model-repair", sizeof(header) - 1);
        f.write(header, 80);

        const auto& sm = mesh.cgal();
        uint32_t num_tris = static_cast<uint32_t>(sm.number_of_faces());
        f.write(reinterpret_cast<const char*>(&num_tris), 4);

        for (auto face : sm.faces())
        {
            auto he = sm.halfedge(face);
            auto v0 = sm.source(he);
            auto v1 = sm.target(he);
            auto v2 = sm.target(sm.next(he));

            const auto& p0 = sm.point(v0);
            const auto& p1 = sm.point(v1);
            const auto& p2 = sm.point(v2);

            // Compute face normal (not normalised — slicers recompute anyway).
            auto n = CGAL::cross_product(p1 - p0, p2 - p0);
            float fn[3] = {static_cast<float>(n.x()),
                           static_cast<float>(n.y()),
                           static_cast<float>(n.z())};
            f.write(reinterpret_cast<const char*>(fn), 12);

            for (const auto* p : {&p0, &p1, &p2})
            {
                float fv[3] = {static_cast<float>(p->x()),
                               static_cast<float>(p->y()),
                               static_cast<float>(p->z())};
                f.write(reinterpret_cast<const char*>(fv), 12);
            }
            uint16_t attr = 0;
            f.write(reinterpret_cast<const char*>(&attr), 2);
        }
    }
    else
    {
        std::ofstream f(path);
        if (!f)
            throw std::runtime_error("Cannot write file: " + path.string());

        f << "solid model-repair\n";
        f << std::scientific;
        const auto& sm = mesh.cgal();
        for (auto face : sm.faces())
        {
            auto he = sm.halfedge(face);
            auto v0 = sm.source(he);
            auto v1 = sm.target(he);
            auto v2 = sm.target(sm.next(he));
            const auto& p0 = sm.point(v0);
            const auto& p1 = sm.point(v1);
            const auto& p2 = sm.point(v2);
            auto n = CGAL::cross_product(p1 - p0, p2 - p0);
            f << "  facet normal " << n.x() << " " << n.y() << " " << n.z() << "\n";
            f << "    outer loop\n";
            for (const auto* p : {&p0, &p1, &p2})
                f << "      vertex " << p->x() << " " << p->y() << " " << p->z() << "\n";
            f << "    endloop\n";
            f << "  endfacet\n";
        }
        f << "endsolid model-repair\n";
    }
}

} // namespace modelrepair::io

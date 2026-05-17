#include "modelrepair/io/ObjIO.hpp"

#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair::io
{

Mesh read_obj(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        throw std::runtime_error("File not found: " + path.string());

    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path.string());

    std::vector<Point3> points;
    std::vector<std::vector<std::size_t>> polygons;

    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v")
        {
            double x, y, z;
            ss >> x >> y >> z;
            points.emplace_back(x, y, z);
        }
        else if (token == "f")
        {
            std::vector<std::size_t> poly;
            std::string idx_str;
            while (ss >> idx_str)
            {
                // Handle "v", "v/vt", "v/vt/vn", "v//vn"
                std::size_t slash = idx_str.find('/');
                int idx = std::stoi(idx_str.substr(0, slash));
                // OBJ indices are 1-based; negative means relative.
                if (idx < 0)
                    poly.push_back(static_cast<std::size_t>(points.size()) + idx);
                else
                    poly.push_back(static_cast<std::size_t>(idx) - 1);
            }
            if (poly.size() >= 3)
                polygons.push_back(std::move(poly));
        }
    }

    PMP::orient_polygon_soup(points, polygons);

    Mesh mesh;
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh.cgal());
    return mesh;
}

void write_obj(const Mesh& mesh, const std::filesystem::path& path)
{
    std::ofstream f(path);
    if (!f)
        throw std::runtime_error("Cannot write file: " + path.string());

    f << "# model-repair\n";
    f << std::fixed;

    const auto& sm = mesh.cgal();

    // Write vertices with stable index mapping.
    std::unordered_map<SurfMesh::Vertex_index, std::size_t> vmap;
    std::size_t idx = 1;
    for (auto v : sm.vertices())
    {
        const auto& p = sm.point(v);
        f << "v " << p.x() << " " << p.y() << " " << p.z() << "\n";
        vmap[v] = idx++;
    }

    for (auto face : sm.faces())
    {
        f << "f";
        for (auto v : sm.vertices_around_face(sm.halfedge(face)))
            f << " " << vmap.at(v);
        f << "\n";
    }
}

} // namespace modelrepair::io

#include "modelrepair/io/ObjIO.hpp"

#include <CGAL/IO/Color.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <algorithm>
#include <cstdint>
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
    std::vector<CGAL::IO::Color> soup_colors;  // parallel to points; empty = no colors
    bool any_color = false;

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
            // Some exporters (Meshlab, CloudCompare) write "v x y z r g b"
            // with r/g/b in [0,1]. We collect them here; they are attached
            // as a "v:color" property map after mesh construction.
            double r, g, b;
            if (ss >> r >> g >> b)
            {
                auto clamp_u8 = [](double v) -> std::uint8_t {
                    return static_cast<std::uint8_t>(
                        std::clamp(static_cast<int>(v * 255.0 + 0.5), 0, 255));
                };
                soup_colors.emplace_back(clamp_u8(r), clamp_u8(g), clamp_u8(b));
                any_color = true;
            }
            else
            {
                soup_colors.emplace_back();  // neutral default
            }
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

    // Attach vertex colors when at least one "v x y z r g b" line was seen.
    if (any_color && soup_colors.size() == points.size())
    {
        auto [cmap, ok] = mesh.cgal().add_property_map<SurfMesh::Vertex_index, CGAL::IO::Color>(
            "v:color", CGAL::IO::Color(128, 128, 128));
        if (ok)
        {
            for (std::size_t i = 0; i < soup_colors.size(); ++i)
                cmap[SurfMesh::Vertex_index(static_cast<SurfMesh::size_type>(i))] = soup_colors[i];
        }
    }

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

    auto cmap_opt = sm.property_map<SurfMesh::Vertex_index, CGAL::IO::Color>("v:color");
    const bool has_colors = cmap_opt.has_value();

    // Write vertices with stable index mapping.
    std::unordered_map<SurfMesh::Vertex_index, std::size_t> vmap;
    std::size_t idx = 1;
    for (auto v : sm.vertices())
    {
        const auto& p = sm.point(v);
        if (has_colors)
        {
            const auto& c = cmap_opt.value()[v];
            f << "v " << p.x() << " " << p.y() << " " << p.z()
              << " " << (c.red()   / 255.0)
              << " " << (c.green() / 255.0)
              << " " << (c.blue()  / 255.0) << "\n";
        }
        else
        {
            f << "v " << p.x() << " " << p.y() << " " << p.z() << "\n";
        }
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

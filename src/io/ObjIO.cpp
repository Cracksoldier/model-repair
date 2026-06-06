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

    std::vector<Point3>  points;
    std::vector<std::vector<std::size_t>> polygons;
    std::vector<CGAL::IO::Color> soup_colors;  // parallel to points; empty = no colors
    bool any_color  = false;  // true if at least one "v x y z r g b" line was seen
    bool all_colors = true;   // false as soon as any "v x y z" line lacks rgb

    // UV coordinate parsing
    std::vector<UV2>             uv_coords;
    // Per-face per-corner UV index; -1 means no UV for that corner.
    std::vector<std::vector<int>> face_uv_indices;
    bool any_uv  = false;
    bool all_uvs = true;  // false the moment any corner lacks a UV index

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
                soup_colors.emplace_back();  // placeholder (color-less vertex)
                all_colors = false;
            }
        }
        else if (token == "vt")
        {
            float u, v_coord;
            ss >> u >> v_coord;
            uv_coords.push_back({u, v_coord});
        }
        else if (token == "f")
        {
            std::vector<std::size_t> poly;
            std::vector<int>         uv_idx;
            std::string idx_str;
            while (ss >> idx_str)
            {
                // Parse "v", "v/vt", "v/vt/vn", "v//vn"
                std::size_t slash1 = idx_str.find('/');
                int v_raw = std::stoi(idx_str.substr(0, slash1));
                // OBJ indices are 1-based; negative means relative.
                if (v_raw < 0)
                    poly.push_back(static_cast<std::size_t>(points.size()) + v_raw);
                else
                    poly.push_back(static_cast<std::size_t>(v_raw) - 1);

                // Extract UV index (component between first and second slash)
                if (slash1 != std::string::npos)
                {
                    std::size_t slash2 = idx_str.find('/', slash1 + 1);
                    std::string vt_part = idx_str.substr(slash1 + 1,
                        slash2 == std::string::npos ? slash2 : slash2 - slash1 - 1);
                    if (!vt_part.empty())
                    {
                        int vt_raw = std::stoi(vt_part);
                        int vt_i   = vt_raw < 0
                            ? static_cast<int>(uv_coords.size()) + vt_raw
                            : vt_raw - 1;
                        uv_idx.push_back(vt_i);
                        any_uv = true;
                    }
                    else
                    {
                        uv_idx.push_back(-1);  // "v//vn" — no UV for this corner
                        all_uvs = false;
                    }
                }
                else
                {
                    uv_idx.push_back(-1);  // "v" only — no UV
                    all_uvs = false;
                }
            }
            if (poly.size() >= 3)
            {
                polygons.push_back(std::move(poly));
                face_uv_indices.push_back(std::move(uv_idx));
            }
        }
    }

    // Accumulate per-vertex UV BEFORE orient_polygon_soup so that the corner→vertex
    // correspondence in face_uv_indices is still valid.  orient_polygon_soup may
    // reverse polygon winding (changing polygons[fi][ci] without touching
    // face_uv_indices[fi][ci]) or remove duplicate polygons, both of which would
    // mis-map UVs if the accumulation happened after the call.
    const std::size_t n_pts = points.size();
    std::vector<float> uv_sum_u(n_pts, 0.f), uv_sum_v(n_pts, 0.f);
    std::vector<int>   uv_cnt(n_pts, 0);
    if (any_uv && all_uvs && !uv_coords.empty())
    {
        for (std::size_t fi = 0; fi < polygons.size(); ++fi)
        {
            for (std::size_t ci = 0; ci < polygons[fi].size(); ++ci)
            {
                const std::size_t vi  = polygons[fi][ci];
                const int         vti = face_uv_indices[fi][ci];
                if (vi < n_pts && vti >= 0 && vti < static_cast<int>(uv_coords.size()))
                {
                    uv_sum_u[vi] += uv_coords[vti][0];
                    uv_sum_v[vi] += uv_coords[vti][1];
                    ++uv_cnt[vi];
                }
            }
        }
    }

    // Snapshot before orient_polygon_soup, which may append points for non-manifold fans.
    const std::size_t n_points_before_orient = points.size();
    const std::size_t n_colors = soup_colors.size();
    PMP::orient_polygon_soup(points, polygons);

    Mesh mesh;
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh.cgal());

    // Attach vertex colors only when every vertex had rgb (all_colors) and the
    // soup had the expected size before orient added any extra points.
    if (any_color && all_colors && n_colors == n_points_before_orient)
    {
        auto [cmap, ok] = mesh.cgal().add_property_map<SurfMesh::Vertex_index, CGAL::IO::Color>(
            "v:color", CGAL::IO::Color(128, 128, 128));
        if (ok)
        {
            for (std::size_t i = 0; i < n_colors; ++i)
                cmap[SurfMesh::Vertex_index(static_cast<SurfMesh::size_type>(i))] = soup_colors[i];
        }
    }

    // Attach pre-computed per-vertex UV map, same guard as colors.
    if (any_uv && all_uvs && !uv_coords.empty()
        && mesh.cgal().number_of_vertices() == n_points_before_orient)
    {
        auto [umap, ok] = mesh.cgal().add_property_map<SurfMesh::Vertex_index, UV2>(
            "v:uv", UV2{0.f, 0.f});
        if (ok)
        {
            for (std::size_t i = 0; i < n_points_before_orient; ++i)
            {
                if (uv_cnt[i] > 0)
                    umap[SurfMesh::Vertex_index(static_cast<SurfMesh::size_type>(i))] =
                        {uv_sum_u[i] / uv_cnt[i], uv_sum_v[i] / uv_cnt[i]};
            }
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

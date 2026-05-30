#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"

#include <CGAL/IO/Color.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <functional>
#include <unordered_map>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace
{

// Save "v:color" as a position→color lookup so it can survive the mesh rebuild.
// Uses exact double equality for Point3 (EPICK stores coords as doubles).
struct Point3Hash {
    std::size_t operator()(const modelrepair::Point3& p) const noexcept {
        const double x = CGAL::to_double(p.x());
        const double y = CGAL::to_double(p.y());
        const double z = CGAL::to_double(p.z());
        std::size_t h = std::hash<double>{}(x);
        h ^= std::hash<double>{}(y) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<double>{}(z) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};
struct Point3Equal {
    bool operator()(const modelrepair::Point3& a, const modelrepair::Point3& b) const noexcept {
        return a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
    }
};
using ColorByPos = std::unordered_map<modelrepair::Point3, CGAL::IO::Color, Point3Hash, Point3Equal>;

} // namespace

namespace modelrepair
{

StepReport RepairPipeline::step_merge_vertices(Mesh& mesh)
{
    StepReport sr;

    // Preserve "v:color" property map across the soup rebuild.
    ColorByPos pos_to_color;
    {
        auto cmap_opt = mesh.cgal().property_map<SurfMesh::Vertex_index, CGAL::IO::Color>("v:color");
        if (cmap_opt.has_value())
        {
            const auto& cmap = cmap_opt.value();
            for (auto v : mesh.cgal().vertices())
                pos_to_color.emplace(mesh.cgal().point(v), cmap[v]);
        }
    }

    // Convert Surface_mesh to polygon soup for CGAL's soup repair functions.
    std::vector<Point3> points;
    std::vector<std::vector<std::size_t>> polygons;

    points.reserve(mesh.num_vertices());
    for (auto v : mesh.cgal().vertices())
        points.push_back(mesh.cgal().point(v));

    polygons.reserve(mesh.num_faces());
    for (auto f : mesh.cgal().faces())
    {
        std::vector<std::size_t> poly;
        for (auto v : mesh.cgal().vertices_around_face(mesh.cgal().halfedge(f)))
            poly.push_back(static_cast<std::size_t>(v));
        polygons.push_back(std::move(poly));
    }

    const std::size_t verts_before = points.size();

    // repair_polygon_soup: merges duplicate points, removes degenerate polygons
    // (fewer than 3 unique vertices after merging), and removes duplicates.
    // This is safer than merge_duplicate_points_in_polygon_soup alone because
    // zero-area triangles collapse to invalid polygons that crash polygon_soup_to_polygon_mesh.
    PMP::repair_polygon_soup(points, polygons);

    const std::size_t verts_after = points.size();
    sr.issues_found = (verts_before > verts_after) ? (verts_before - verts_after) : 0;
    sr.issues_fixed = sr.issues_found;

    PMP::orient_polygon_soup(points, polygons);

    mesh.clear();
    PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh.cgal());

    // Re-attach vertex colors from the position lookup (if any were present).
    if (!pos_to_color.empty())
    {
        auto [cmap, ok] = mesh.cgal().add_property_map<SurfMesh::Vertex_index, CGAL::IO::Color>(
            "v:color", CGAL::IO::Color(128, 128, 128));
        if (ok)
        {
            for (auto v : mesh.cgal().vertices())
            {
                auto it = pos_to_color.find(mesh.cgal().point(v));
                if (it != pos_to_color.end())
                    cmap[v] = it->second;
            }
        }
    }

    return sr;
}

} // namespace modelrepair

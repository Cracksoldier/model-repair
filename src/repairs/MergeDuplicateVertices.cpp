#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"

#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

StepReport RepairPipeline::step_merge_vertices(Mesh& mesh)
{
    StepReport sr;

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

    return sr;
}

} // namespace modelrepair

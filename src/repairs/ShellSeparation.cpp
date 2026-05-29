#include "modelrepair/ShellSeparation.hpp"

#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

#include <boost/property_map/property_map.hpp>

#include <algorithm>
#include <chrono>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

namespace
{

// Build a face→component_id map without modifying the (possibly const) mesh.
// Returns the number of components.
std::size_t build_component_map(
    const SurfMesh& sm,
    std::unordered_map<SurfMesh::Face_index, std::size_t>& comp_map)
{
    comp_map.reserve(sm.number_of_faces());
    auto pmap = boost::associative_property_map(comp_map);
    return PMP::connected_components(sm, pmap);
}

} // namespace

ShellSeparationResult analyze_shells(const Mesh& mesh)
{
    const auto& sm = mesh.cgal();
    ShellSeparationResult result;

    if (sm.number_of_faces() == 0)
        return result;

    auto t0 = std::chrono::steady_clock::now();

    std::unordered_map<SurfMesh::Face_index, std::size_t> comp_map;
    std::size_t num = build_component_map(sm, comp_map);

    result.components_found = num;
    result.shells.resize(num);

    // Count faces per component
    for (auto f : sm.faces())
        result.shells[comp_map[f]].face_count++;

    // Determine which components are closed (no border halfedge in that component)
    std::vector<bool> closed(num, true);
    for (auto h : sm.halfedges()) {
        if (sm.is_border(h)) {
            auto opp = sm.opposite(h);
            if (!sm.is_border(opp)) {
                auto f = sm.face(opp);
                closed[comp_map[f]] = false;
            }
        }
    }
    for (std::size_t i = 0; i < num; ++i)
        result.shells[i].is_closed = closed[i];

    // Sort shells largest-first
    std::sort(result.shells.begin(), result.shells.end(),
              [](const ShellInfo& a, const ShellInfo& b) {
                  return a.face_count > b.face_count;
              });

    auto t1 = std::chrono::steady_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

ShellSeparationResult keep_shells(Mesh& mesh, std::size_t keep_n)
{
    auto& sm = mesh.cgal();
    ShellSeparationResult result;

    if (sm.number_of_faces() == 0)
        return result;

    auto t0 = std::chrono::steady_clock::now();

    // Analyse first so we can return info
    std::unordered_map<SurfMesh::Face_index, std::size_t> comp_map;
    result.components_found = build_component_map(sm, comp_map);

    result.shells.resize(result.components_found);
    for (auto f : sm.faces())
        result.shells[comp_map[f]].face_count++;

    std::sort(result.shells.begin(), result.shells.end(),
              [](const ShellInfo& a, const ShellInfo& b) {
                  return a.face_count > b.face_count;
              });

    PMP::keep_largest_connected_components(sm, keep_n);

    auto t1 = std::chrono::steady_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

std::vector<Mesh> split_shells(const Mesh& mesh)
{
    const auto& sm = mesh.cgal();

    std::unordered_map<SurfMesh::Face_index, std::size_t> comp_map;
    std::size_t num = build_component_map(sm, comp_map);

    if (num == 0)
        return {};

    // Count faces per component to sort largest-first
    std::vector<std::size_t> face_count(num, 0);
    for (auto f : sm.faces())
        face_count[comp_map[f]]++;

    std::vector<std::size_t> order(num);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return face_count[a] > face_count[b];
    });

    std::vector<Mesh> result;
    result.reserve(num);

    for (std::size_t comp : order)
    {
        std::vector<Point3> points;
        std::vector<std::vector<std::size_t>> polygons;
        std::unordered_map<SurfMesh::Vertex_index, std::size_t> v_remap;

        for (auto f : sm.faces())
        {
            if (comp_map[f] != comp)
                continue;

            auto he = sm.halfedge(f);
            std::vector<std::size_t> tri;
            tri.reserve(3);
            for (int i = 0; i < 3; ++i) {
                auto v = sm.target(he);
                auto [it, inserted] = v_remap.emplace(v, points.size());
                if (inserted)
                    points.push_back(sm.point(v));
                tri.push_back(it->second);
                he = sm.next(he);
            }
            polygons.push_back(std::move(tri));
        }

        PMP::orient_polygon_soup(points, polygons);

        Mesh m;
        PMP::polygon_soup_to_polygon_mesh(points, polygons, m.cgal());
        result.push_back(std::move(m));
    }

    return result;
}

} // namespace modelrepair

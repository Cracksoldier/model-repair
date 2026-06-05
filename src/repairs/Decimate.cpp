#include "modelrepair/Decimate.hpp"

#include <CGAL/IO/Color.h>
#include <CGAL/Surface_mesh_simplification/edge_collapse.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Face_count_ratio_stop_predicate.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>

#ifdef MODELREPAIR_HAVE_MESHOPTIMIZER
#include <meshoptimizer.h>
#endif

#ifdef MODELREPAIR_HAVE_OPENMESH
// OpenMesh headers must come after CGAL headers to avoid macro conflicts
#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>
#include <OpenMesh/Tools/Decimater/DecimaterT.hh>
#include <OpenMesh/Tools/Decimater/ModQuadricT.hh>
#include <OpenMesh/Tools/Decimater/ModNormalDeviationT.hh>
#endif

#include <chrono>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace SMS = CGAL::Surface_mesh_simplification;
namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

bool decimate_meshoptimizer_available()
{
#ifdef MODELREPAIR_HAVE_MESHOPTIMIZER
    return true;
#else
    return false;
#endif
}

bool decimate_openmesh_available()
{
#ifdef MODELREPAIR_HAVE_OPENMESH
    return true;
#else
    return false;
#endif
}

// ─── CGAL backend ─────────────────────────────────────────────────────────────

static DecimateResult decimate_cgal(Mesh& mesh, double ratio)
{
    auto& sm = mesh.cgal();
    DecimateResult r;
    r.faces_before = sm.number_of_faces();
    r.backend_used = DecimateBackend::CGAL;

    auto t0 = std::chrono::steady_clock::now();
    SMS::edge_collapse(sm, SMS::Face_count_ratio_stop_predicate<SurfMesh>(ratio, sm));
    auto t1 = std::chrono::steady_clock::now();

    r.faces_after = sm.number_of_faces();
    r.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

// ─── meshoptimizer backend ────────────────────────────────────────────────────

#ifdef MODELREPAIR_HAVE_MESHOPTIMIZER
static DecimateResult decimate_meshopt(Mesh& mesh, const DecimateParams& params)
{
    const auto& sm = mesh.cgal();
    DecimateResult r;
    r.faces_before = sm.number_of_faces();
    r.backend_used = DecimateBackend::MeshOptimizer;

    // Time the entire operation including conversions (fix: was timing only meshopt_simplify).
    auto t0 = std::chrono::steady_clock::now();

    // Save v:color by position index so it survives the polygon-soup rebuild.
    const auto cmap_opt = sm.property_map<SurfMesh::Vertex_index, CGAL::IO::Color>("v:color");
    const bool has_color = cmap_opt.has_value();
    std::vector<CGAL::IO::Color> pos_colors;
    if (has_color)
        pos_colors.resize(sm.number_of_vertices());

    // Build dense float vertex array + indexed triangle list.
    std::vector<float> positions;
    positions.reserve(sm.number_of_vertices() * 3);
    std::unordered_map<SurfMesh::Vertex_index, uint32_t> v_remap;
    v_remap.reserve(sm.number_of_vertices());

    for (auto v : sm.vertices()) {
        const auto& p = sm.point(v);
        const uint32_t idx = static_cast<uint32_t>(positions.size() / 3);
        v_remap[v] = idx;
        if (has_color)
            pos_colors[idx] = cmap_opt.value()[v];
        positions.push_back(static_cast<float>(CGAL::to_double(p.x())));
        positions.push_back(static_cast<float>(CGAL::to_double(p.y())));
        positions.push_back(static_cast<float>(CGAL::to_double(p.z())));
    }

    std::vector<uint32_t> src_indices;
    src_indices.reserve(sm.number_of_faces() * 3);
    for (auto f : sm.faces()) {
        auto he = sm.halfedge(f);
        for (int i = 0; i < 3; ++i) {
            src_indices.push_back(v_remap.at(sm.target(he)));
            he = sm.next(he);
        }
    }

    const std::size_t nv        = positions.size() / 3;
    const std::size_t raw_ic    = static_cast<std::size_t>(src_indices.size() * params.ratio);
    const std::size_t target_ic = (raw_ic / 3) * 3;  // must be multiple of 3

    std::vector<uint32_t> dst_indices(src_indices.size());
    float result_error = 0.0f;

    const std::size_t new_ic = meshopt_simplify(
        dst_indices.data(),
        src_indices.data(), src_indices.size(),
        positions.data(), nv, sizeof(float) * 3,
        target_ic,
        static_cast<float>(params.target_error),
        0,
        &result_error);
    dst_indices.resize(new_ic);

    // Build pts only from vertices referenced by dst_indices, avoiding isolated
    // vertices that polygon_soup_to_polygon_mesh would otherwise add (insert_isolated_vertices
    // defaults to true in CGAL 6.x).
    std::unordered_map<uint32_t, std::size_t> used_verts;
    used_verts.reserve(new_ic);
    std::vector<Point3> pts;

    for (std::size_t i = 0; i < new_ic; ++i) {
        const uint32_t idx = dst_indices[i];
        if (!used_verts.count(idx)) {
            used_verts[idx] = pts.size();
            pts.emplace_back(positions[idx * 3],
                             positions[idx * 3 + 1],
                             positions[idx * 3 + 2]);
        }
    }

    // Build reverse map for color restore: new pts index → original position index.
    std::vector<uint32_t> pts_to_orig;
    if (has_color) {
        pts_to_orig.resize(pts.size());
        for (const auto& [orig, newi] : used_verts)
            pts_to_orig[newi] = orig;
    }

    std::vector<std::vector<std::size_t>> faces;
    faces.reserve(new_ic / 3);
    for (std::size_t i = 0; i < new_ic; i += 3)
        faces.push_back({used_verts[dst_indices[i]],
                         used_verts[dst_indices[i + 1]],
                         used_verts[dst_indices[i + 2]]});

    PMP::orient_polygon_soup(pts, faces);
    SurfMesh new_sm;
    PMP::polygon_soup_to_polygon_mesh(pts, faces, new_sm);

    // Restore v:color. polygon_soup_to_polygon_mesh adds vertices in pts order,
    // so new_sm.vertex(j) corresponds to pts[j] for a clean manifold soup.
    if (has_color && !pts_to_orig.empty()) {
        auto [cmap, ok] = new_sm.add_property_map<SurfMesh::Vertex_index, CGAL::IO::Color>(
            "v:color", CGAL::IO::Color(128, 128, 128));
        if (ok) {
            std::size_t j = 0;
            for (auto v : new_sm.vertices()) {
                if (j < pts_to_orig.size())
                    cmap[v] = pos_colors[pts_to_orig[j++]];
            }
        }
    }

    mesh.cgal() = std::move(new_sm);
    auto t1 = std::chrono::steady_clock::now();

    r.faces_after = mesh.cgal().number_of_faces();
    r.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}
#endif // MODELREPAIR_HAVE_MESHOPTIMIZER

// ─── OpenMesh backend ─────────────────────────────────────────────────────────

#ifdef MODELREPAIR_HAVE_OPENMESH
static DecimateResult decimate_openmesh(Mesh& mesh, const DecimateParams& params)
{
    using OMesh     = OpenMesh::TriMesh_ArrayKernelT<>;
    using Decimater = OpenMesh::Decimater::DecimaterT<OMesh>;

    const auto& sm = mesh.cgal();
    DecimateResult r;
    r.faces_before = sm.number_of_faces();
    r.backend_used = DecimateBackend::OpenMesh;

    // Time the entire operation including conversions (fix: was timing only decimate_to_faces).
    auto t0 = std::chrono::steady_clock::now();

    // Save v:color by OM vertex handle index so it survives the polygon-soup rebuild.
    const auto cmap_opt = sm.property_map<SurfMesh::Vertex_index, CGAL::IO::Color>("v:color");
    const bool has_color = cmap_opt.has_value();
    std::unordered_map<int, CGAL::IO::Color> vh_to_color;
    if (has_color)
        vh_to_color.reserve(sm.number_of_vertices());

    // CGAL → OpenMesh
    OMesh om;
    om.request_face_status();
    om.request_edge_status();
    om.request_vertex_status();

    std::unordered_map<int, OMesh::VertexHandle> v_map;
    v_map.reserve(sm.number_of_vertices());
    for (auto v : sm.vertices()) {
        const auto& p = sm.point(v);
        OMesh::VertexHandle vh = om.add_vertex(OMesh::Point(
            static_cast<float>(CGAL::to_double(p.x())),
            static_cast<float>(CGAL::to_double(p.y())),
            static_cast<float>(CGAL::to_double(p.z()))));
        v_map[v.idx()] = vh;
        if (has_color)
            vh_to_color[vh.idx()] = cmap_opt.value()[v];
    }
    for (auto f : sm.faces()) {
        auto he = sm.halfedge(f);
        OMesh::VertexHandle vhs[3];
        for (int i = 0; i < 3; ++i) {
            vhs[i] = v_map.at(sm.target(he).idx());
            he = sm.next(he);
        }
        om.add_face(vhs[0], vhs[1], vhs[2]);
    }

    const std::size_t target_faces =
        static_cast<std::size_t>(static_cast<double>(om.n_faces()) * params.ratio);

    {
        Decimater decimater(om);
        OpenMesh::Decimater::ModQuadricT<OMesh>::Handle hq;
        decimater.add(hq);
        decimater.module(hq).unset_max_err();

        if (params.normal_deviation < 179.0) {
            OpenMesh::Decimater::ModNormalDeviationT<OMesh>::Handle hn;
            decimater.add(hn);
            decimater.module(hn).set_normal_deviation(
                static_cast<float>(params.normal_deviation));
        }

        decimater.initialize();
        decimater.decimate_to_faces(target_faces);
    }
    om.garbage_collection();

    // OpenMesh → CGAL via polygon soup.
    // Track pts_colors in parallel with pts so colors survive in pts order.
    std::vector<Point3> pts;
    std::unordered_map<int, std::size_t> vh_to_idx;
    pts.reserve(om.n_vertices());
    std::vector<CGAL::IO::Color> pts_colors;
    if (has_color)
        pts_colors.reserve(om.n_vertices());

    for (auto vit = om.vertices_begin(); vit != om.vertices_end(); ++vit) {
        if (!om.status(*vit).deleted()) {
            vh_to_idx[vit->idx()] = pts.size();
            const auto& p = om.point(*vit);
            pts.emplace_back(static_cast<double>(p[0]),
                             static_cast<double>(p[1]),
                             static_cast<double>(p[2]));
            if (has_color) {
                auto it = vh_to_color.find(vit->idx());
                pts_colors.push_back(it != vh_to_color.end()
                    ? it->second
                    : CGAL::IO::Color(128, 128, 128));
            }
        }
    }

    std::vector<std::vector<std::size_t>> faces;
    faces.reserve(om.n_faces());
    for (auto fit = om.faces_begin(); fit != om.faces_end(); ++fit) {
        if (!om.status(*fit).deleted()) {
            std::vector<std::size_t> tri;
            tri.reserve(3);
            for (auto fvit = om.cfv_iter(*fit); fvit.is_valid(); ++fvit)
                tri.push_back(vh_to_idx.at(fvit->idx()));
            faces.push_back(std::move(tri));
        }
    }

    PMP::orient_polygon_soup(pts, faces);
    SurfMesh new_sm;
    PMP::polygon_soup_to_polygon_mesh(pts, faces, new_sm);

    // Restore v:color. polygon_soup_to_polygon_mesh adds vertices in pts order,
    // so new_sm.vertex(j) corresponds to pts[j] for a clean manifold soup.
    if (has_color && !pts_colors.empty()) {
        auto [cmap, ok] = new_sm.add_property_map<SurfMesh::Vertex_index, CGAL::IO::Color>(
            "v:color", CGAL::IO::Color(128, 128, 128));
        if (ok) {
            std::size_t j = 0;
            for (auto v : new_sm.vertices()) {
                if (j < pts_colors.size())
                    cmap[v] = pts_colors[j++];
            }
        }
    }

    mesh.cgal() = std::move(new_sm);
    auto t1 = std::chrono::steady_clock::now();

    r.faces_after = mesh.cgal().number_of_faces();
    r.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}
#endif // MODELREPAIR_HAVE_OPENMESH

// ─── dispatch ─────────────────────────────────────────────────────────────────

DecimateResult decimate(Mesh& mesh, double ratio)
{
    return decimate(mesh, DecimateParams{DecimateBackend::CGAL, ratio});
}

DecimateResult decimate(Mesh& mesh, const DecimateParams& params)
{
    switch (params.backend) {
#ifdef MODELREPAIR_HAVE_MESHOPTIMIZER
        case DecimateBackend::MeshOptimizer:
            return decimate_meshopt(mesh, params);
#endif
#ifdef MODELREPAIR_HAVE_OPENMESH
        case DecimateBackend::OpenMesh:
            return decimate_openmesh(mesh, params);
#endif
        default:
            if (params.backend != DecimateBackend::CGAL)
                std::fprintf(stderr,
                    "model-repair: requested decimation backend not compiled in; "
                    "falling back to CGAL\n");
            return decimate_cgal(mesh, params.ratio);
    }
}

} // namespace modelrepair

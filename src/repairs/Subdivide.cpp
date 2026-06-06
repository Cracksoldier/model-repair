#include "modelrepair/Subdivide.hpp"

#include <CGAL/subdivision_method_3.h>

#include <chrono>

namespace SMS3 = CGAL::Subdivision_method_3;

namespace modelrepair
{

SubdivisionResult subdivide(Mesh& mesh, unsigned int iterations, SubdivisionMethod method)
{
    auto& sm = mesh.cgal();
    SubdivisionResult r;
    r.faces_before = sm.number_of_faces();

    auto t0 = std::chrono::steady_clock::now();

    // Run one iteration at a time so UV coordinates can be propagated to new vertices.
    const bool has_uv = mesh.has_uv();
    const auto one_iter = CGAL::parameters::number_of_iterations(1);

    for (unsigned int i = 0; i < iterations; ++i)
    {
        const auto n_before = static_cast<SurfMesh::size_type>(sm.number_of_vertices());

        switch (method) {
            case SubdivisionMethod::Loop:
                SMS3::Loop_subdivision(sm, one_iter);
                break;
            case SubdivisionMethod::CatmullClark:
                SMS3::CatmullClark_subdivision(sm, one_iter);
                break;
        }

        // Interpolate UV for vertices added by this subdivision pass.
        // Pass 1: average from OLD neighbours only (correct for Loop edge-midpoints).
        // Pass 2: for vertices that got no old neighbours (Catmull-Clark face centroids
        //         are adjacent only to other new edge-midpoint vertices), average from
        //         all neighbours — those new neighbours already have UVs from pass 1.
        if (has_uv)
        {
            auto uv_opt = sm.property_map<SurfMesh::Vertex_index, UV2>("v:uv");
            if (uv_opt.has_value())
            {
                auto& uv_map = *uv_opt;
                bool need_second_pass = false;

                for (auto v : sm.vertices())
                {
                    if (static_cast<SurfMesh::size_type>(v) < n_before)
                        continue;
                    if (sm.is_isolated(v) || sm.halfedge(v) == SurfMesh::null_halfedge())
                        continue;

                    float su = 0.f, sv = 0.f;
                    int   cnt = 0;
                    for (auto h : CGAL::halfedges_around_target(sm.halfedge(v), sm))
                    {
                        auto nb = sm.source(h);
                        if (static_cast<SurfMesh::size_type>(nb) < n_before)
                        {
                            su  += uv_map[nb][0];
                            sv  += uv_map[nb][1];
                            ++cnt;
                        }
                    }
                    if (cnt > 0)
                        uv_map[v] = {su / cnt, sv / cnt};
                    else
                        need_second_pass = true;
                }

                // Second pass: vertices with no old neighbours (Catmull-Clark face
                // centroids) average from all 1-ring neighbours, which now have valid
                // UVs from the first pass.
                if (need_second_pass)
                {
                    for (auto v : sm.vertices())
                    {
                        if (static_cast<SurfMesh::size_type>(v) < n_before)
                            continue;
                        if (sm.is_isolated(v) || sm.halfedge(v) == SurfMesh::null_halfedge())
                            continue;
                        // Skip vertices already assigned in pass 1.
                        if (uv_map[v][0] != 0.f || uv_map[v][1] != 0.f)
                            continue;

                        float su = 0.f, sv = 0.f;
                        int   cnt = 0;
                        for (auto h : CGAL::halfedges_around_target(sm.halfedge(v), sm))
                        {
                            auto nb = sm.source(h);
                            su  += uv_map[nb][0];
                            sv  += uv_map[nb][1];
                            ++cnt;
                        }
                        if (cnt > 0)
                            uv_map[v] = {su / cnt, sv / cnt};
                    }
                }
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    r.faces_after  = sm.number_of_faces();
    r.duration_ms  = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return r;
}

} // namespace modelrepair

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
        // Each new vertex is placed at an edge midpoint whose two endpoint vertices
        // (both "old") are the only reliable UV sources at this stage.
        if (has_uv)
        {
            auto uv_opt = sm.property_map<SurfMesh::Vertex_index, UV2>("v:uv");
            if (uv_opt.has_value())
            {
                auto& uv_map = *uv_opt;
                for (auto v : sm.vertices())
                {
                    if (static_cast<SurfMesh::size_type>(v) < n_before)
                        continue;  // old vertex — UV already valid
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

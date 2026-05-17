#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"

#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/Polygon_mesh_processing/refine.h>

#include <unordered_set>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

StepReport RepairPipeline::step_fill_holes(Mesh& mesh)
{
    StepReport sr;

    auto& sm = mesh.cgal();

    // Collect one halfedge per hole by full boundary-cycle traversal.
    std::vector<SurfMesh::Halfedge_index> border_cycles;
    std::unordered_set<SurfMesh::Halfedge_index> visited;
    for (auto he : sm.halfedges())
    {
        if (!sm.is_border(he) || visited.count(he))
            continue;
        // Count boundary edges in this cycle.
        std::size_t count = 0;
        auto cur = he;
        do
        {
            visited.insert(cur);
            ++count;
            cur = sm.next(cur);
        } while (cur != he);

        if (opts_.max_hole_edges == 0 || count <= opts_.max_hole_edges)
            border_cycles.push_back(he);
    }

    sr.issues_found = border_cycles.size();

    std::size_t filled = 0;
    for (auto h : border_cycles)
    {
        std::vector<SurfMesh::Face_index>   patch_faces;
        std::vector<SurfMesh::Vertex_index> patch_verts;

        if (opts_.fill_holes_smooth)
        {
            PMP::triangulate_and_refine_hole(sm, h,
                std::back_inserter(patch_faces),
                std::back_inserter(patch_verts));
        }
        else
        {
            PMP::triangulate_hole(sm, h, std::back_inserter(patch_faces));
        }

        if (!patch_faces.empty())
            ++filled;
    }

    sr.issues_fixed = filled;
    return sr;
}

} // namespace modelrepair

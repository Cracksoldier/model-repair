#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"

#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

StepReport RepairPipeline::step_fix_non_manifold(Mesh& mesh)
{
    StepReport sr;

    // non_manifold_vertices outputs halfedge_descriptors, one per problem vertex.
    std::vector<SurfMesh::Halfedge_index> nm_halfedges;
    PMP::non_manifold_vertices(mesh.cgal(), std::back_inserter(nm_halfedges));
    sr.issues_found = nm_halfedges.size();

    if (!nm_halfedges.empty())
    {
        // duplicate_non_manifold_vertices splits each non-manifold vertex into
        // as many copies as needed so every face fan becomes disk-topology.
        std::size_t fixed = PMP::duplicate_non_manifold_vertices(mesh.cgal());
        sr.issues_fixed   = fixed;
    }

    return sr;
}

} // namespace modelrepair

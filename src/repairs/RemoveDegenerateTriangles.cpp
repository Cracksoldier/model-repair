#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"

#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

StepReport RepairPipeline::step_remove_degenerate(Mesh& mesh)
{
    StepReport sr;

    const std::size_t faces_before = mesh.num_faces();

    // Collect degenerate faces first so we can count them.
    std::vector<SurfMesh::Face_index> degenerate;
    for (auto f : mesh.cgal().faces())
    {
        if (PMP::is_degenerate_triangle_face(f, mesh.cgal()))
            degenerate.push_back(f);
    }
    sr.issues_found = degenerate.size();

    if (!degenerate.empty())
    {
        PMP::remove_degenerate_faces(mesh.cgal());
        const std::size_t faces_after = mesh.num_faces();
        sr.issues_fixed = (faces_before > faces_after) ? (faces_before - faces_after) : 0;
    }

    return sr;
}

} // namespace modelrepair

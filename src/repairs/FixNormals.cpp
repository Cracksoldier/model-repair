#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"

#include <CGAL/Polygon_mesh_processing/orientation.h>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

StepReport RepairPipeline::step_fix_normals(Mesh& mesh)
{
    StepReport sr;

    // is_outward_oriented asserts is_closed, so skip the check on open meshes.
    // PMP::orient still works on open meshes — it orients each component
    // consistently regardless of whether there are boundary edges.
    bool needs_orient = true;
    if (mesh.is_closed())
        needs_orient = !PMP::is_outward_oriented(mesh.cgal());

    sr.issues_found = needs_orient ? 1 : 0;

    if (needs_orient)
    {
        PMP::orient(mesh.cgal());
        sr.issues_fixed = 1;
    }

    return sr;
}

} // namespace modelrepair

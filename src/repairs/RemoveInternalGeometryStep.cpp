#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"
#include "modelrepair/RemoveInternalGeometry.hpp"

namespace modelrepair
{

StepReport RepairPipeline::step_remove_internal_geometry(Mesh& mesh)
{
    StepReport sr;

    if (!mesh.is_closed())
        return sr;

    auto r = remove_internal_geometry(mesh);
    sr.issues_found = r.faces_removed;
    sr.issues_fixed = r.faces_removed;
    return sr;
}

} // namespace modelrepair

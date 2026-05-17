#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/boost/graph/copy_face_graph.h>

namespace PMP = CGAL::Polygon_mesh_processing;

// Self-intersection removal requires exact constructions kernel for robustness.
using EKernel   = CGAL::Exact_predicates_exact_constructions_kernel;
using EPoint3   = EKernel::Point_3;
using ESurfMesh = CGAL::Surface_mesh<EPoint3>;

namespace modelrepair
{

StepReport RepairPipeline::step_remove_self_intersections(Mesh& mesh)
{
    StepReport sr;

    // Detect self-intersections on the EPICK mesh first (fast).
    std::vector<std::pair<SurfMesh::Face_index, SurfMesh::Face_index>> intersecting;
    PMP::self_intersections(mesh.cgal(), std::back_inserter(intersecting));
    sr.issues_found = intersecting.size();

    if (intersecting.empty())
        return sr;

    // Convert to EPECK mesh for the repair operation.
    ESurfMesh emesh;
    CGAL::copy_face_graph(mesh.cgal(), emesh);

    bool ok = PMP::experimental::remove_self_intersections(emesh,
        CGAL::parameters::preserve_genus(false));

    // Convert back to EPICK.
    mesh.clear();
    CGAL::copy_face_graph(emesh, mesh.cgal());

    if (ok)
    {
        std::vector<std::pair<SurfMesh::Face_index, SurfMesh::Face_index>> remaining;
        PMP::self_intersections(mesh.cgal(), std::back_inserter(remaining));
        sr.issues_fixed = sr.issues_found - remaining.size();
    }

    return sr;
}

} // namespace modelrepair

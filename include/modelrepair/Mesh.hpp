#pragma once

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <cstddef>

namespace modelrepair
{

using Kernel   = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point3   = Kernel::Point_3;
using SurfMesh = CGAL::Surface_mesh<Point3>;

class Mesh
{
public:
    SurfMesh&       cgal();
    const SurfMesh& cgal() const;

    std::size_t num_vertices() const;
    std::size_t num_faces() const;

    bool is_valid() const;      // combinatorial validity
    bool is_closed() const;     // no boundary halfedges
    bool is_manifold() const;   // no non-manifold vertices or edges

    void clear();

private:
    SurfMesh mesh_;
};

} // namespace modelrepair

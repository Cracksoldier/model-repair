#include "modelrepair/Mesh.hpp"

#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <vector>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

SurfMesh& Mesh::cgal()
{
    return mesh_;
}

const SurfMesh& Mesh::cgal() const
{
    return mesh_;
}

std::size_t Mesh::num_vertices() const
{
    return static_cast<std::size_t>(mesh_.number_of_vertices());
}

std::size_t Mesh::num_faces() const
{
    return static_cast<std::size_t>(mesh_.number_of_faces());
}

bool Mesh::is_valid() const
{
    return mesh_.is_valid();
}

bool Mesh::is_closed() const
{
    for (auto he : mesh_.halfedges())
    {
        if (mesh_.is_border(he))
            return false;
    }
    return true;
}

bool Mesh::is_manifold() const
{
    // non_manifold_vertices outputs halfedge_descriptors marking problem vertices.
    std::vector<SurfMesh::Halfedge_index> nm;
    PMP::non_manifold_vertices(mesh_, std::back_inserter(nm));
    return nm.empty();
}

void Mesh::clear()
{
    mesh_.clear();
}

} // namespace modelrepair

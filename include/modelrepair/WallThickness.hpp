#pragma once

#include "Mesh.hpp"
#include <vector>

namespace modelrepair
{

// Returns per-face wall thickness in mesh units (typically mm).
// For each face, a ray is cast from the centroid along the inward normal;
// the distance to the first intersection is the local wall thickness.
// Returns an empty vector if the mesh has no faces.
// Values of std::numeric_limits<double>::max() indicate no opposite wall was found.
std::vector<double> analyze_wall_thickness(const Mesh& mesh);

} // namespace modelrepair

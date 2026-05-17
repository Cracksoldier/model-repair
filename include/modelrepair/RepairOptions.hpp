#pragma once

#include <cstddef>

namespace modelrepair
{

struct RepairOptions
{
    // Step 1 — vertex merging
    bool   merge_duplicate_vertices    = true;
    double merge_tolerance             = 1e-6;

    // Step 2 — degenerate geometry
    bool   remove_degenerate_triangles = true;

    // Step 3 — non-manifold repair
    bool   fix_non_manifold            = true;

    // Step 4 — normal orientation
    bool   fix_normals                 = true;

    // Step 5 — hole filling
    bool   fill_holes                  = true;
    std::size_t max_hole_edges         = 0;    // 0 = no limit
    bool   fill_holes_smooth           = true; // false = flat fan triangulation

    // Step 6 — self-intersections (slow; uses EPECK kernel internally)
    bool   remove_self_intersections   = true;

    bool   verbose                     = false;
};

} // namespace modelrepair

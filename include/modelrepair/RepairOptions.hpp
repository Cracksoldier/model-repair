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

    // Step 6 — self-intersections (very slow on large meshes; uses EPECK kernel internally)
    bool   remove_self_intersections   = false;

    // Post-repair remeshing (run before smooth; introduces new geometry)
    bool         remesh                    = false;
    double       remesh_edge_length_factor = 0.8;
    unsigned int remesh_iterations         = 3;

    // Post-repair smoothing
    bool         smooth            = false;
    unsigned int smooth_iterations = 3;    // angle-smoothing passes
    double       smooth_crease_angle = 45.0; // degrees; edges sharper than this are preserved

    // Post-repair decimation
    bool   decimate       = false;
    double decimate_ratio = 0.5;   // fraction of faces to retain (0 < ratio ≤ 1)

    // Diagnose only — run detection without modifying the mesh
    bool   diagnose_only  = false;

    bool   verbose                     = false;
};

} // namespace modelrepair

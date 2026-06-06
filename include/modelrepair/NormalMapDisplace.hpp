#pragma once

#include "Mesh.hpp"
#include <string>

namespace modelrepair
{

struct NormalMapDisplaceParams
{
    std::string normal_map_path;
    float displacement_strength = 0.3f;   // in mesh units (mm)
    int   pre_subdivisions      = 2;       // Loop-subdivision passes before displacing
    bool  flip_green            = false;   // flip G channel for DirectX normal maps
    bool  flip_y_uv             = true;    // image Y is top-to-bottom; UV Y is bottom-to-top
};

struct NormalMapDisplaceResult
{
    int   faces_before = 0;
    int   faces_after  = 0;
    float duration_ms  = 0.f;
};

// Displace mesh vertices using pseudo-height derived from a tangent-space normal map.
// Requires the mesh to have a "v:uv" vertex property map (load from OBJ with UV data).
// The blue/Z channel encodes surface slope: flat regions (nz≈1) → no displacement;
// maximally tilted regions (nz≈-1) → displacement_strength mm of outward push.
// Throws std::runtime_error when the mesh has no UV map or the image cannot be loaded.
NormalMapDisplaceResult displace_from_normal_map(Mesh& mesh,
                                                  const NormalMapDisplaceParams& params);

} // namespace modelrepair

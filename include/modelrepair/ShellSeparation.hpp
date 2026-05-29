#pragma once

#include "Mesh.hpp"
#include <cstddef>
#include <vector>

namespace modelrepair
{

struct ShellInfo
{
    std::size_t face_count = 0;
    bool        is_closed  = false;
};

struct ShellSeparationResult
{
    std::size_t            components_found = 0;
    std::vector<ShellInfo> shells;      // sorted largest-first
    double                 duration_ms = 0.0;
};

// Analyse connected components without modifying the mesh.
ShellSeparationResult analyze_shells(const Mesh& mesh);

// Keep the N largest connected components, remove the rest.
ShellSeparationResult keep_shells(Mesh& mesh, std::size_t keep_n = 1);

// Split mesh into one Mesh per connected component (sorted largest-first).
std::vector<Mesh> split_shells(const Mesh& mesh);

} // namespace modelrepair

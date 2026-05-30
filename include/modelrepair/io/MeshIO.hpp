#pragma once

#include "../Mesh.hpp"
#include <filesystem>
#include <string>

namespace modelrepair::io
{

// Format is inferred from the file extension (.stl, .obj, .3mf, .glb, .gltf, .ply).
// Throws std::runtime_error for unsupported or missing files.
Mesh  load(const std::filesystem::path& path);
void  save(const Mesh& mesh, const std::filesystem::path& path, bool binary_stl = true);

std::string infer_format(const std::filesystem::path& path);

} // namespace modelrepair::io

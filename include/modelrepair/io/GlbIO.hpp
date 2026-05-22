#pragma once

#include "../Mesh.hpp"
#include <filesystem>

namespace modelrepair::io
{

Mesh read_glb(const std::filesystem::path& path);
void write_glb(const Mesh& mesh, const std::filesystem::path& path);

} // namespace modelrepair::io

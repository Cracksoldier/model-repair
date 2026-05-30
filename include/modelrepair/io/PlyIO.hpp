#pragma once

#include "../Mesh.hpp"
#include <filesystem>

namespace modelrepair::io
{

Mesh read_ply(const std::filesystem::path& path);
void write_ply(const Mesh& mesh, const std::filesystem::path& path);

} // namespace modelrepair::io

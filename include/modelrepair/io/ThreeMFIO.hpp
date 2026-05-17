#pragma once

#include "../Mesh.hpp"
#include <filesystem>

namespace modelrepair::io
{

// Reads the first mesh object from a 3MF file.
Mesh read_3mf(const std::filesystem::path& path);

void write_3mf(const Mesh& mesh, const std::filesystem::path& path);

} // namespace modelrepair::io

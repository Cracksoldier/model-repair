#pragma once

#include "../Mesh.hpp"
#include <filesystem>

namespace modelrepair::io
{

// Accepts both binary and ASCII STL automatically.
Mesh read_stl(const std::filesystem::path& path);

// binary=true writes compact binary STL; false writes ASCII.
void write_stl(const Mesh& mesh, const std::filesystem::path& path, bool binary = true);

} // namespace modelrepair::io

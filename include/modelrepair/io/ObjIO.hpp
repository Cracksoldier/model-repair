#pragma once

#include "../Mesh.hpp"
#include <filesystem>

namespace modelrepair::io
{

Mesh read_obj(const std::filesystem::path& path);
void write_obj(const Mesh& mesh, const std::filesystem::path& path);

} // namespace modelrepair::io

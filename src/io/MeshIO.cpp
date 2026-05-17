#include "modelrepair/io/MeshIO.hpp"
#include "modelrepair/io/StlIO.hpp"
#include "modelrepair/io/ObjIO.hpp"
#include "modelrepair/io/ThreeMFIO.hpp"

#include <algorithm>
#include <stdexcept>

namespace modelrepair::io
{

std::string infer_format(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

Mesh load(const std::filesystem::path& path)
{
    const std::string ext = infer_format(path);
    if (ext == ".stl")  return read_stl(path);
    if (ext == ".obj")  return read_obj(path);
    if (ext == ".3mf")  return read_3mf(path);
    throw std::runtime_error("Unsupported file format '" + ext + "': " + path.string());
}

void save(const Mesh& mesh, const std::filesystem::path& path, bool binary_stl)
{
    const std::string ext = infer_format(path);
    if (ext == ".stl") { write_stl(mesh, path, binary_stl); return; }
    if (ext == ".obj") { write_obj(mesh, path); return; }
    if (ext == ".3mf") { write_3mf(mesh, path); return; }
    throw std::runtime_error("Unsupported file format '" + ext + "': " + path.string());
}

} // namespace modelrepair::io

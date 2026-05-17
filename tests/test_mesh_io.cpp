#include <catch2/catch_test_macros.hpp>
#include "modelrepair/io/MeshIO.hpp"
#include "modelrepair/io/StlIO.hpp"

#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;
using namespace modelrepair;
using namespace modelrepair::io;

static const fs::path MESH_DIR = TEST_MESH_DIR;

TEST_CASE("STL binary round-trip preserves topology", "[io][stl]")
{
    auto mesh = read_stl(MESH_DIR / "valid_cube.stl");
    REQUIRE(mesh.num_vertices() > 0);
    REQUIRE(mesh.num_faces() == 12);

    auto tmp = fs::temp_directory_path() / "mr_test_cube.stl";
    write_stl(mesh, tmp, true);

    auto reloaded = read_stl(tmp);
    CHECK(reloaded.num_vertices() == mesh.num_vertices());
    CHECK(reloaded.num_faces()    == mesh.num_faces());
    fs::remove(tmp);
}

TEST_CASE("STL ASCII round-trip", "[io][stl]")
{
    auto mesh = read_stl(MESH_DIR / "valid_cube.stl");
    auto tmp = fs::temp_directory_path() / "mr_test_cube_ascii.stl";
    write_stl(mesh, tmp, false);

    auto reloaded = read_stl(tmp);
    CHECK(reloaded.num_faces() == mesh.num_faces());
    fs::remove(tmp);
}

TEST_CASE("load() infers format from extension", "[io]")
{
    auto mesh = load(MESH_DIR / "valid_cube.stl");
    CHECK(mesh.num_faces() == 12);
}

TEST_CASE("load() throws on missing file", "[io]")
{
    CHECK_THROWS_AS(load(MESH_DIR / "does_not_exist.stl"), std::runtime_error);
}

TEST_CASE("load() throws on unsupported extension", "[io]")
{
    auto tmp = fs::temp_directory_path() / "test.xyz";
    { std::ofstream f(tmp); f << "dummy"; }
    CHECK_THROWS_AS(load(tmp), std::runtime_error);
    fs::remove(tmp);
}

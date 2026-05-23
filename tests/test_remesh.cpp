#include <catch2/catch_test_macros.hpp>
#include "modelrepair/Remesh.hpp"
#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/io/MeshIO.hpp"

#include <cmath>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace modelrepair;
using namespace modelrepair::io;

static const fs::path MESH_DIR = TEST_MESH_DIR;

// STL is polygon soup until the repair pipeline runs (which merges duplicate
// vertices). Volume is only defined on closed meshes.
static Mesh load_and_repair(const fs::path& path)
{
    auto mesh = load(path);
    RepairOptions opts;
    opts.remove_self_intersections = false;
    RepairPipeline(opts).run(mesh);
    return mesh;
}

TEST_CASE("Remesh subdivides a closed mesh and preserves volume", "[remesh]")
{
    auto mesh = load_and_repair(MESH_DIR / "valid_cube.stl");
    auto vol_before = mesh.volume();
    REQUIRE(vol_before.has_value());
    REQUIRE(*vol_before > 0.0);

    const std::size_t f_before = mesh.num_faces();
    auto rr = remesh(mesh, /*edge_length_factor=*/0.5, /*iterations=*/2);

    CHECK(rr.faces_before == f_before);
    CHECK(rr.faces_after  > f_before);
    CHECK(rr.duration_ms  >= 0.0);

    auto vol_after = mesh.volume();
    REQUIRE(vol_after.has_value());
    REQUIRE(*vol_after > 0.0);
    const double rel_diff = std::abs(*vol_after - *vol_before) / *vol_before;
    CHECK(rel_diff < 0.01);  // volume preserved within 1 %
}

TEST_CASE("Remesh callback fires once per iteration", "[remesh]")
{
    auto mesh = load_and_repair(MESH_DIR / "valid_cube.stl");
    std::vector<unsigned int> indices;

    remesh(mesh, 0.5, 3, /*sharp_feature_angle=*/45.0,
           [&](unsigned int i) { indices.push_back(i); });

    REQUIRE(indices.size() == 3);
    CHECK(indices[0] == 1);
    CHECK(indices[1] == 2);
    CHECK(indices[2] == 3);
}

TEST_CASE("Remesh on open mesh skips volume restore without crashing", "[remesh]")
{
    // Don't repair — we want the mesh to remain open for this test.
    auto mesh = load(MESH_DIR / "open_surface.stl");
    REQUIRE_FALSE(mesh.volume().has_value()); // open => no volume

    auto rr = remesh(mesh, 0.5, 1);
    CHECK(rr.duration_ms >= 0.0);
    // No assertion fails / no crash = pass; the function should have early-skipped
    // the volume preservation block.
}

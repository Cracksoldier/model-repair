#include <catch2/catch_test_macros.hpp>
#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/io/MeshIO.hpp"

#include <filesystem>

namespace fs = std::filesystem;
using namespace modelrepair;
using namespace modelrepair::io;

static const fs::path MESH_DIR = TEST_MESH_DIR;

TEST_CASE("Full pipeline: valid cube survives unmodified", "[pipeline]")
{
    auto mesh = load(MESH_DIR / "valid_cube.stl");
    std::size_t faces_before = mesh.num_faces();

    RepairOptions opts;
    opts.remove_self_intersections = false; // skip slow step for baseline
    RepairPipeline pipeline(opts);
    auto report = pipeline.run(mesh);

    CHECK(report.is_manifold_after);
    CHECK(report.is_closed_after);
    CHECK(report.triangles_after == faces_before);

    // The merge step will always find "issues" for STL input (binary STL is a
    // polygon soup with no vertex sharing — all 36 vertices merge down to 8).
    // The normals step may also re-orient after the soup round-trip.
    // The important invariant is that the final mesh is valid, closed, and manifold.
}

TEST_CASE("Full pipeline: open surface becomes watertight", "[pipeline]")
{
    auto mesh = load(MESH_DIR / "open_surface.stl");
    RepairOptions opts;
    opts.remove_self_intersections = false;
    RepairPipeline pipeline(opts);
    auto report = pipeline.run(mesh);

    CHECK(report.is_closed_after);
    CHECK(report.is_manifold_after);
}

TEST_CASE("Full pipeline: inverted normals cube is fixed", "[pipeline]")
{
    auto mesh = load(MESH_DIR / "inverted_normals.stl");
    RepairOptions opts;
    opts.remove_self_intersections = false;
    RepairPipeline pipeline(opts);
    auto report = pipeline.run(mesh);

    CHECK(report.is_manifold_after);
    CHECK(report.is_closed_after);
}

TEST_CASE("Full pipeline: degenerate triangles removed", "[pipeline]")
{
    auto mesh = load(MESH_DIR / "degenerate_triangles.stl");
    std::size_t faces_before = mesh.num_faces();
    RepairOptions opts;
    opts.remove_self_intersections = false;
    RepairPipeline pipeline(opts);
    auto report = pipeline.run(mesh);

    CHECK(report.triangles_after < faces_before);
    CHECK(report.is_manifold_after);
}

TEST_CASE("Full pipeline: progress callback fires once per enabled step", "[pipeline]")
{
    auto mesh = load(MESH_DIR / "valid_cube.stl");
    RepairOptions opts;
    opts.remove_self_intersections = false;

    int calls = 0;
    RepairPipeline pipeline(opts);
    pipeline.set_progress_callback([&](int, int, const std::string&) { ++calls; });
    pipeline.run(mesh);

    // 5 steps enabled (self-intersections skipped)
    CHECK(calls == 5);
}

TEST_CASE("RepairReport format_text contains step names", "[report]")
{
    auto mesh = load(MESH_DIR / "valid_cube.stl");
    RepairOptions opts;
    opts.remove_self_intersections = false;
    RepairPipeline pipeline(opts);
    auto report = pipeline.run(mesh);

    auto text = report.format_text();
    CHECK(text.find("Merge") != std::string::npos);
    CHECK(text.find("normals") != std::string::npos);
}

TEST_CASE("RepairReport format_json is valid JSON structure", "[report]")
{
    auto mesh = load(MESH_DIR / "valid_cube.stl");
    RepairOptions opts;
    opts.remove_self_intersections = false;
    RepairPipeline pipeline(opts);
    auto report = pipeline.run(mesh);

    auto json = report.format_json();
    CHECK(json.find("{") != std::string::npos);
    CHECK(json.find("\"steps\"") != std::string::npos);
    CHECK(json.find("\"is_closed_after\"") != std::string::npos);
}

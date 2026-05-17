#include <catch2/catch_test_macros.hpp>
#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/io/MeshIO.hpp"

#include <filesystem>

namespace fs = std::filesystem;
using namespace modelrepair;
using namespace modelrepair::io;

static const fs::path MESH_DIR = TEST_MESH_DIR;

// Helper: run only one step.
static RepairReport run_step(Mesh& mesh, RepairOptions opts)
{
    RepairPipeline p(opts);
    return p.run(mesh);
}

TEST_CASE("Merge duplicate vertices reduces vertex count", "[repair][merge]")
{
    auto mesh = load(MESH_DIR / "duplicate_vertices.stl");
    std::size_t verts_before = mesh.num_vertices();

    RepairOptions opts;
    opts.remove_degenerate_triangles = false;
    opts.fix_non_manifold            = false;
    opts.fix_normals                 = false;
    opts.fill_holes                  = false;
    opts.remove_self_intersections   = false;

    auto report = run_step(mesh, opts);
    CHECK(report.vertices_after < verts_before);

    auto& step = report.steps[0]; // merge step
    CHECK(step.was_run);
    CHECK(step.issues_found > 0);
}

TEST_CASE("Remove degenerate triangles finds exactly 5 injected faces", "[repair][degen]")
{
    auto mesh = load(MESH_DIR / "degenerate_triangles.stl");
    const std::size_t faces_before = mesh.num_faces(); // 12 valid + 5 degenerate = 17

    RepairOptions opts;
    opts.merge_duplicate_vertices    = true;
    opts.fix_non_manifold            = false;
    opts.fix_normals                 = false;
    opts.fill_holes                  = false;
    opts.remove_self_intersections   = false;

    auto report = run_step(mesh, opts);

    // The 5 degenerate triangles may be removed by repair_polygon_soup during
    // the merge step (step 1) or by remove_degenerate_faces in step 2.
    // Either way, the final face count must be 12 (only the valid cube faces remain).
    CHECK(report.triangles_after == 12);
    CHECK(report.triangles_after < faces_before);
}

TEST_CASE("Fix normals on inverted cube", "[repair][normals]")
{
    auto mesh = load(MESH_DIR / "inverted_normals.stl");

    RepairOptions opts;
    opts.merge_duplicate_vertices    = true;
    opts.remove_degenerate_triangles = true;
    opts.fix_non_manifold            = true;
    opts.fix_normals                 = true;
    opts.fill_holes                  = false;
    opts.remove_self_intersections   = false;

    auto report = run_step(mesh, opts);
    auto& step = report.steps[3]; // normals step
    CHECK(step.was_run);
    // After fixing, the mesh should be closed and manifold.
    CHECK(report.is_closed_after);
}

TEST_CASE("Fill holes closes an open surface", "[repair][holes]")
{
    auto mesh = load(MESH_DIR / "open_surface.stl");
    REQUIRE_FALSE(mesh.is_closed());

    RepairOptions opts;
    opts.remove_self_intersections = false;

    auto report = run_step(mesh, opts);
    CHECK(report.is_closed_after);
    auto& step = report.steps[4]; // fill holes step
    CHECK(step.was_run);
    CHECK(step.issues_found > 0);
    CHECK(step.issues_fixed > 0);
}

TEST_CASE("Fix non-manifold repairs shared edges", "[repair][manifold]")
{
    auto mesh = load(MESH_DIR / "non_manifold_edge.stl");

    RepairOptions opts;
    opts.fix_normals               = false;
    opts.fill_holes                = false;
    opts.remove_self_intersections = false;

    auto report = run_step(mesh, opts);
    CHECK(report.is_manifold_after);
    auto& step = report.steps[2]; // non-manifold step
    CHECK(step.was_run);
}

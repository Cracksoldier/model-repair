#include "modelrepair/RepairPipeline.hpp"

#include <CGAL/Polygon_mesh_processing/measure.h>

#include <chrono>

namespace PMP = CGAL::Polygon_mesh_processing;

namespace modelrepair
{

namespace
{

double elapsed_ms(std::chrono::steady_clock::time_point start)
{
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

RepairPipeline::RepairPipeline(RepairOptions opts) : opts_(std::move(opts)) {}

void RepairPipeline::set_progress_callback(ProgressCallback cb)
{
    progress_cb_ = std::move(cb);
}

void RepairPipeline::notify(int step, int total, const std::string& name)
{
    if (progress_cb_)
        progress_cb_(step, total, name);
}

RepairReport RepairPipeline::run(Mesh& mesh)
{
    RepairReport report;

    // Capture before-state
    report.vertices_before     = mesh.num_vertices();
    report.triangles_before    = mesh.num_faces();
    report.surface_area_before = CGAL::to_double(PMP::area(mesh.cgal()));
    if (mesh.is_closed())
        report.volume_before   = CGAL::to_double(PMP::volume(mesh.cgal()));

    // Diagnose mode: work on a copy so the caller's mesh is untouched
    Mesh working_copy;
    if (opts_.diagnose_only) working_copy = mesh;
    Mesh& work = opts_.diagnose_only ? working_copy : mesh;

    constexpr int total_steps = 6;
    int step = 0;

    auto run_step = [&](const std::string& name, auto fn, bool enabled) -> StepReport
    {
        ++step;
        StepReport sr;
        sr.name    = name;
        sr.was_run = enabled;
        if (!enabled)
            return sr;

        notify(step, total_steps, name);
        auto t0  = std::chrono::steady_clock::now();
        sr        = fn();
        sr.name   = name;
        sr.was_run = true;
        sr.duration_ms = elapsed_ms(t0);
        return sr;
    };

    report.steps.push_back(run_step("Merge duplicate vertices",    [&] { return step_merge_vertices(work); },            opts_.merge_duplicate_vertices));
    report.steps.push_back(run_step("Remove degenerate triangles", [&] { return step_remove_degenerate(work); },         opts_.remove_degenerate_triangles));
    report.steps.push_back(run_step("Fix non-manifold geometry",   [&] { return step_fix_non_manifold(work); },          opts_.fix_non_manifold));
    report.steps.push_back(run_step("Fix face normals",            [&] { return step_fix_normals(work); },               opts_.fix_normals));
    report.steps.push_back(run_step("Fill holes",                  [&] { return step_fill_holes(work); },                opts_.fill_holes));
    report.steps.push_back(run_step("Remove self-intersections",   [&] { return step_remove_self_intersections(work); }, opts_.remove_self_intersections));

    // Capture after-state
    report.vertices_after      = work.num_vertices();
    report.triangles_after     = work.num_faces();
    report.surface_area_after  = CGAL::to_double(PMP::area(work.cgal()));
    if (work.is_closed())
        report.volume_after    = CGAL::to_double(PMP::volume(work.cgal()));
    report.is_valid_after      = work.is_valid();
    report.is_manifold_after   = work.is_manifold();
    report.is_closed_after     = work.is_closed();
    report.diagnose_only       = opts_.diagnose_only;

    return report;
}

} // namespace modelrepair

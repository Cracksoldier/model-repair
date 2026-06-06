#include "modelrepair/Decimate.hpp"
#include "modelrepair/Remesh.hpp"
#include "modelrepair/ShellSeparation.hpp"
#include "modelrepair/Smooth.hpp"
#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"
#include "modelrepair/Version.hpp"
#include "modelrepair/io/MeshIO.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace modelrepair;

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string fmt_ms(double ms)
{
    auto s = static_cast<long long>(ms / 1000.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld", s / 60, s % 60);
    return buf;
}

static std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

static int issues_fixed(const RepairReport& r)
{
    int n = 0;
    for (const auto& s : r.steps)
        n += s.issues_fixed;
    return n;
}

static void print_shell_analysis(const ShellSeparationResult& sr)
{
    std::cout << "Shells: " << sr.components_found << " connected component(s)\n";
    for (std::size_t i = 0; i < sr.shells.size(); ++i)
        std::cout << "  [" << i << "] " << sr.shells[i].face_count << " faces"
                  << (sr.shells[i].is_closed ? ", closed" : ", open") << "\n";
}

// ── single-file repair ────────────────────────────────────────────────────────

static int run_single_file(
    const fs::path&       input_path,
    const fs::path&       output_path,
    const RepairOptions&  opts,
    bool                  ascii_stl,
    bool                  quiet,
    bool                  verbose,
    const std::string&    report_path,
    const std::string&    report_format,
    const DecimateParams& decimate_params,
    unsigned int          smooth_iters,
    bool                  smooth_vulkan,
    bool                  analyze_shells_flag,
    bool                  keep_largest_shell,
    const fs::path&       export_shells_dir,
    std::shared_ptr<spdlog::logger> logger)
{
    // Load
    modelrepair::Mesh mesh;
    try
    {
        logger->info("Loading {}", input_path.string());
        mesh = modelrepair::io::load(input_path);
        logger->info("Loaded {} vertices, {} triangles",
                     mesh.num_vertices(), mesh.num_faces());
    }
    catch (const std::exception& e)
    {
        logger->error("Load failed: {}", e.what());
        return 1;
    }

    // Repair
    RepairPipeline pipeline(opts);

    if (verbose)
    {
        pipeline.set_progress_callback(
            [&](int step, int total, const std::string& name)
            {
                logger->debug("[{}/{}] {}", step, total, name);
            });
    }

    RepairReport report;
    try
    {
        report = pipeline.run(mesh);
    }
    catch (const std::exception& e)
    {
        logger->error("Repair failed: {}", e.what());
        return 3;
    }

    // Remesh (after repair, before smooth)
    if (opts.remesh && !opts.diagnose_only)
    {
        modelrepair::RemeshResult rr;
        try
        {
            rr = modelrepair::remesh(mesh, opts.remesh_edge_length_factor, opts.remesh_iterations,
                                     opts.smooth_crease_angle);
        }
        catch (const std::exception& e)
        {
            logger->error("Remeshing failed: {}", e.what());
            return 7;
        }
        modelrepair::StepReport sr;
        sr.name         = "Remesh";
        sr.was_run      = true;
        const int remesh_delta = static_cast<int>(rr.faces_after) - static_cast<int>(rr.faces_before);
        sr.issues_fixed = remesh_delta > 0 ? static_cast<std::size_t>(remesh_delta) : 0u;
        sr.duration_ms  = rr.duration_ms;
        report.steps.push_back(sr);
    }

    // Smooth (after remesh, before decimate)
    if (smooth_iters > 0 && !opts.diagnose_only)
    {
        modelrepair::SmoothResult smr;
        try
        {
            smr = modelrepair::smooth(mesh, smooth_iters, opts.smooth_crease_angle,
                                      nullptr, smooth_vulkan);
        }
        catch (const std::exception& e)
        {
            logger->error("Smoothing failed: {}", e.what());
            return 6;
        }
        modelrepair::StepReport sr;
        sr.name        = "Smooth";
        sr.was_run     = true;
        sr.duration_ms = smr.duration_ms;
        report.steps.push_back(sr);
    }

    // Decimate (after smooth)
    if (decimate_params.ratio > 0.0 && !opts.diagnose_only)
    {
        // Save pre-decimate intermediate (post-repair and post-smooth)
        fs::path inter = output_path.parent_path()
                       / (output_path.stem().string() + "_repaired"
                          + output_path.extension().string());
        if (!output_path.empty())
        {
            try
            {
                modelrepair::io::save(mesh, inter, !ascii_stl);
                logger->info("Pre-decimate mesh saved to {}", inter.string());
            }
            catch (const std::exception& e)
            {
                logger->warn("Could not save pre-decimate intermediate: {}", e.what());
            }
        }

        modelrepair::DecimateResult dr;
        try
        {
            dr = modelrepair::decimate(mesh, decimate_params);
        }
        catch (const std::exception& e)
        {
            logger->error("Decimation failed: {}", e.what());
            return 5;
        }
        modelrepair::StepReport sr;
        sr.name         = "Decimate";
        sr.was_run      = true;
        const int dec_delta = static_cast<int>(dr.faces_before) - static_cast<int>(dr.faces_after);
        sr.issues_fixed = dec_delta > 0 ? static_cast<std::size_t>(dec_delta) : 0u;
        sr.duration_ms  = dr.duration_ms;
        report.steps.push_back(sr);
        report.triangles_after    = mesh.num_faces();
        report.surface_area_after = mesh.surface_area();
        report.volume_after       = mesh.volume();
    }

    // Shell analysis / separation (after all other ops)
    if (analyze_shells_flag || keep_largest_shell || !export_shells_dir.empty())
    {
        auto sr = modelrepair::analyze_shells(mesh);
        if (!quiet)
        {
            if (opts.diagnose_only)
                std::cout << "Shell analysis (pre-repair, original mesh):\n";
            print_shell_analysis(sr);
        }

        // split_shells is read-only — allowed in diagnose mode
        if (!export_shells_dir.empty())
        {
            if (!fs::is_directory(export_shells_dir))
            {
                logger->error("Export-shells directory does not exist: {}",
                              export_shells_dir.string());
                return 8;
            }
            auto shells = modelrepair::split_shells(mesh);
            const std::string stem = input_path.stem().string();
            const std::string ext  = output_path.empty()
                                     ? ".stl"
                                     : output_path.extension().string();
            for (std::size_t i = 0; i < shells.size(); ++i)
            {
                fs::path shell_path = export_shells_dir
                                    / (stem + "_shell_" + std::to_string(i) + ext);
                try {
                    modelrepair::io::save(shells[i], shell_path, !ascii_stl);
                    logger->info("Shell {} saved to {}", i, shell_path.string());
                } catch (const std::exception& e) {
                    logger->error("Failed to save shell {}: {}", i, e.what());
                    return 8;
                }
            }
        }

        // keep_shells mutates the mesh — no effect in diagnose mode
        if (keep_largest_shell)
        {
            if (!opts.diagnose_only)
                modelrepair::keep_shells(mesh, 1);
            else
                logger->warn("--keep-largest-shell has no effect in --diagnose mode");
        }
    }

    // Print report
    if (!quiet)
    {
        if (report_format == "json")
            std::cout << report.format_json();
        else
            std::cout << report.format_text();
    }

    // Save JSON report if requested
    if (!report_path.empty())
    {
        std::ofstream rf(report_path);
        if (!rf)
        {
            logger->error("Cannot write report: {}", report_path);
            return 4;
        }
        rf << report.format_json();
        logger->info("Report written to {}", report_path);
    }

    // Save output (skipped in diagnose mode, and optional when exporting shells)
    if (!opts.diagnose_only && !output_path.empty())
    {
        try
        {
            logger->info("Saving {}", output_path.string());
            modelrepair::io::save(mesh, output_path, !ascii_stl);
        }
        catch (const std::exception& e)
        {
            logger->error("Save failed: {}", e.what());
            return 4;
        }
    }

    logger->info("Done. Manifold: {}, Closed: {}",
                 report.is_manifold_after ? "yes" : "no",
                 report.is_closed_after   ? "yes" : "no");

    return 0;
}

// ── batch repair ──────────────────────────────────────────────────────────────

struct BatchResult
{
    fs::path   input_path;
    fs::path   output_path;
    RepairReport report;
    std::string error;       // empty = success
    double      duration_ms = 0.0;
};

static fs::path batch_output_path(const fs::path& input, const fs::path& output_dir)
{
    if (!output_dir.empty())
        return output_dir / input.filename();  // same filename in output dir, no suffix
    return input.parent_path() / (input.stem().string() + "_repaired" + input.extension().string());
}

static int run_batch(
    const std::vector<fs::path>& inputs,
    const fs::path&              output_dir,
    const fs::path&              batch_report_path,
    const RepairOptions&         opts,
    bool                         ascii_stl,
    bool                         quiet,
    bool                         verbose,
    const DecimateParams&        decimate_params,
    unsigned int                 smooth_iters,
    bool                         smooth_vulkan,
    bool                         analyze_shells_flag,
    bool                         keep_largest_shell,
    const fs::path&              export_shells_dir,
    std::shared_ptr<spdlog::logger> logger)
{
    if (!output_dir.empty() && !fs::is_directory(output_dir))
    {
        logger->error("Output directory does not exist: {}", output_dir.string());
        return 1;
    }
    if (!export_shells_dir.empty() && !fs::is_directory(export_shells_dir))
    {
        logger->error("Export-shells directory does not exist: {}", export_shells_dir.string());
        return 1;
    }

    const int total = static_cast<int>(inputs.size());
    std::vector<BatchResult> results;
    results.reserve(total);
    int n_ok = 0, n_fail = 0;

    auto wall_start = std::chrono::steady_clock::now();

    for (int idx = 0; idx < total; ++idx)
    {
        const fs::path& in  = inputs[idx];
        const fs::path  out = batch_output_path(in, output_dir);

        BatchResult res;
        res.input_path  = in;
        res.output_path = out;

        auto t0 = std::chrono::steady_clock::now();

        try
        {
            Mesh mesh = modelrepair::io::load(in);

            RepairPipeline pipeline(opts);
            if (verbose)
            {
                pipeline.set_progress_callback(
                    [&](int step, int tot, const std::string& name)
                    { logger->debug("  [{}/{}] {}", step, tot, name); });
            }
            res.report = pipeline.run(mesh);

            if (opts.remesh && !opts.diagnose_only)
            {
                auto rr = modelrepair::remesh(mesh, opts.remesh_edge_length_factor,
                                              opts.remesh_iterations, opts.smooth_crease_angle);
                StepReport sr;
                sr.name         = "Remesh";
                sr.was_run      = true;
                const int remesh_delta = static_cast<int>(rr.faces_after) - static_cast<int>(rr.faces_before);
                sr.issues_fixed = remesh_delta > 0 ? static_cast<std::size_t>(remesh_delta) : 0u;
                sr.duration_ms  = rr.duration_ms;
                res.report.steps.push_back(sr);
            }

            if (smooth_iters > 0 && !opts.diagnose_only)
            {
                auto smr = modelrepair::smooth(mesh, smooth_iters, opts.smooth_crease_angle,
                                               nullptr, smooth_vulkan);
                StepReport sr;
                sr.name        = "Smooth";
                sr.was_run     = true;
                sr.duration_ms = smr.duration_ms;
                res.report.steps.push_back(sr);
            }

            if (decimate_params.ratio > 0.0 && !opts.diagnose_only)
            {
                fs::path inter = out.parent_path()
                               / (out.stem().string() + "_repaired" + out.extension().string());
                try {
                    modelrepair::io::save(mesh, inter, !ascii_stl);
                    logger->info("Pre-decimate mesh saved to {}", inter.string());
                } catch (const std::exception& e) {
                    logger->warn("Could not save pre-decimate intermediate: {}", e.what());
                }

                auto dr = modelrepair::decimate(mesh, decimate_params);
                StepReport sr;
                sr.name         = "Decimate";
                sr.was_run      = true;
                const int dec_delta = static_cast<int>(dr.faces_before) - static_cast<int>(dr.faces_after);
                sr.issues_fixed = dec_delta > 0 ? static_cast<std::size_t>(dec_delta) : 0u;
                sr.duration_ms  = dr.duration_ms;
                res.report.steps.push_back(sr);
                res.report.triangles_after    = mesh.num_faces();
                res.report.surface_area_after = mesh.surface_area();
                res.report.volume_after       = mesh.volume();
            }

            // Shell analysis / separation
            if (analyze_shells_flag || keep_largest_shell || !export_shells_dir.empty())
            {
                auto sr = modelrepair::analyze_shells(mesh);
                if (!quiet)
                {
                    std::cout << in.filename().string();
                    if (opts.diagnose_only) std::cout << " (pre-repair)";
                    std::cout << " — ";
                    print_shell_analysis(sr);
                }

                // split_shells is read-only — allowed in diagnose mode
                if (!export_shells_dir.empty())
                {
                    auto shells = modelrepair::split_shells(mesh);
                    const std::string stem = in.stem().string();
                    const std::string ext  = in.extension().string();
                    for (std::size_t i = 0; i < shells.size(); ++i)
                    {
                        fs::path shell_path = export_shells_dir
                                            / (stem + "_shell_" + std::to_string(i) + ext);
                        try {
                            modelrepair::io::save(shells[i], shell_path, !ascii_stl);
                            logger->info("Shell {} saved to {}", i, shell_path.string());
                        } catch (const std::exception& e) {
                            logger->error("Failed to save shell {}: {}", i, e.what());
                            // log and continue; primary mesh save proceeds normally
                        }
                    }
                }

                // keep_shells mutates the mesh
                if (keep_largest_shell && !opts.diagnose_only)
                    modelrepair::keep_shells(mesh, 1);
            }

            if (!opts.diagnose_only)
                modelrepair::io::save(mesh, out, !ascii_stl);
        }
        catch (const std::exception& e)
        {
            res.error = e.what();
        }

        auto t1 = std::chrono::steady_clock::now();
        res.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // Per-file one-liner
        if (!quiet)
        {
            if (res.error.empty())
            {
                const auto& r = res.report;
                std::cout << "[" << (idx + 1) << "/" << total << "] "
                          << in.filename().string();
                if (opts.diagnose_only)
                    std::cout << "  (diagnose only)";
                else
                    std::cout << "  →  " << out.filename().string();
                std::cout << "   " << fmt_ms(res.duration_ms)
                          << "  " << (r.is_closed_after && r.is_manifold_after ? "✔ watertight" : "✘ open")
                          << "  " << issues_fixed(r) << " issues fixed\n";
            }
            else
            {
                std::cout
                    << "[" << (idx + 1) << "/" << total << "] "
                    << in.filename().string()
                    << "  →  FAILED: " << res.error << "\n";
            }
        }
        else if (!res.error.empty())
        {
            std::cerr << "FAILED [" << in.string() << "]: " << res.error << "\n";
        }

        if (res.error.empty()) ++n_ok; else ++n_fail;
        results.push_back(std::move(res));
    }

    auto wall_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - wall_start).count();

    if (!quiet)
    {
        std::cout << std::string(60, '-') << "\n"
                  << total << " files · " << n_ok << " succeeded · " << n_fail << " failed"
                  << " · total " << fmt_ms(wall_ms) << "\n";
    }

    // Batch JSON report
    if (!batch_report_path.empty())
    {
        std::ofstream rf(batch_report_path);
        if (!rf)
        {
            logger->error("Cannot write batch report: {}", batch_report_path.string());
        }
        else
        {
            rf << "[\n";
            for (std::size_t i = 0; i < results.size(); ++i)
            {
                const auto& r = results[i];
                if (r.error.empty())
                    rf << r.report.format_json();
                else
                    rf << "{\"error\":\"" << json_escape(r.error) << "\",\"input\":\""
                       << json_escape(r.input_path.string()) << "\"}";
                if (i + 1 < results.size()) rf << ",";
                rf << "\n";
            }
            rf << "]\n";
            logger->info("Batch report written to {}", batch_report_path.string());
        }
    }

    return n_fail > 0 ? 1 : 0;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    CLI::App app{"model-repair: Linux 3D mesh repair tool", "model-repair"};
    app.set_version_flag("--version", MODELREPAIR_VERSION);

    // Positional args (single-file mode)
    fs::path input_path, output_path;
    app.add_option("INPUT",  input_path,  "Input mesh (.stl, .obj, .3mf, .glb, .gltf, .ply) — required in single-file mode")->check(CLI::ExistingFile);
    app.add_option("OUTPUT", output_path, "Output mesh (.stl, .obj, .3mf, .glb, .gltf, .ply)");

    // Repair toggles (shared by both single-file and batch modes)
    RepairOptions opts;
    app.add_flag("!--no-merge-vertices",            opts.merge_duplicate_vertices,    "Skip duplicate vertex merging");
    app.add_option("--merge-tolerance",             opts.merge_tolerance,             "Vertex merge distance tolerance")->default_val(1e-6);
    app.add_flag("!--no-remove-degenerate",         opts.remove_degenerate_triangles, "Skip degenerate triangle removal");
    app.add_flag("!--no-fix-non-manifold",          opts.fix_non_manifold,            "Skip non-manifold repair");
    app.add_flag("!--no-fix-normals",               opts.fix_normals,                 "Skip normal orientation fix");
    app.add_flag("!--no-fill-holes",                opts.fill_holes,                  "Skip hole filling");
    app.add_option("--max-hole-edges",              opts.max_hole_edges,              "Skip holes with more than N edges (0=all)")->default_val(0);

    bool flat_fill = false;
    app.add_flag("--flat-fill", flat_fill, "Use flat fan fill instead of smooth hole fill");

    app.add_flag("!--no-remove-self-intersections", opts.remove_self_intersections, "Skip self-intersection removal (slow)");

    bool remove_internal = false;
    app.add_flag("--remove-internal-geometry", remove_internal,
                 "Remove faces whose centroid is inside the mesh volume (pipeline step 7)");

    bool ascii_stl = false;
    app.add_flag("--ascii-stl", ascii_stl, "Write ASCII STL instead of binary");

    std::string report_path;
    app.add_option("--report", report_path, "Write repair report to JSON file");

    std::string report_format = "text";
    app.add_option("--report-format", report_format, "Report format: text or json")->default_val("text");

    bool verbose = false, quiet = false;
    app.add_flag("-v,--verbose", verbose, "Print per-step progress");
    app.add_flag("-q,--quiet",   quiet,   "Suppress all output except errors");

    bool         diagnose      = false;
    double       decimate_ratio = 0.0;
    unsigned int smooth_iters   = 0;
    double       remesh_factor  = 0.0;
    unsigned int remesh_iters   = 3;
    double       smooth_crease  = 45.0;
    bool         smooth_vulkan  = false;

    std::string  decimate_backend_str  = "cgal";
    double       decimate_target_error = 0.01;
    double       decimate_normal_dev   = 15.0;

    bool         analyze_shells_flag = false;
    bool         keep_largest_shell  = false;
    fs::path     export_shells_dir;

    app.add_flag  ("--diagnose", diagnose,
                   "Report issues without modifying the mesh. OUTPUT is optional.");
    app.add_option("--remesh", remesh_factor,
                   "Remesh before smoothing: edge length factor (0.1–2.0).")
       ->check(CLI::Range(0.1, 2.0));
    app.add_option("--remesh-iterations", remesh_iters,
                   "Number of isotropic remesh iterations (default 3).")
       ->check(CLI::Range(1u, 10u));
    app.add_option("--smooth", smooth_iters,
                   "Smooth after repair: number of iterations (1–50).")
       ->check(CLI::Range(1u, 50u));
    app.add_option("--smooth-crease-angle", smooth_crease,
                   "Dihedral angle threshold for smoothing feature preservation (0–180°, default 45).")
       ->check(CLI::Range(0.0, 180.0));
    app.add_flag  ("--smooth-vulkan", smooth_vulkan,
                   "Use GPU (Vulkan) for smoothing when available; falls back to CPU otherwise.");
    app.add_option("--decimate", decimate_ratio,
                   "Decimate after repair: retain this fraction of faces (0.01–1.0).")
       ->check(CLI::Range(0.01, 1.0));
    app.add_option("--decimate-backend", decimate_backend_str,
                   "Decimation backend: cgal (default, slow/accurate), meshoptimizer (fast), openmesh (QEM).")
       ->default_val("cgal");
    app.add_option("--decimate-target-error", decimate_target_error,
                   "MeshOptimizer: relative geometric error budget (0.0001–1.0, default 0.01).")
       ->check(CLI::Range(0.0001, 1.0));
    app.add_option("--decimate-normal-dev", decimate_normal_dev,
                   "OpenMesh: normal deviation limit in degrees (1–90, default 15).")
       ->check(CLI::Range(1.0, 90.0));
    app.add_flag  ("--analyze-shells", analyze_shells_flag,
                   "Print connected-component (shell) analysis of the (repaired) mesh.");
    app.add_flag  ("--keep-largest-shell", keep_largest_shell,
                   "After repair, discard all but the largest connected component.");
    app.add_option("--export-shells", export_shells_dir,
                   "After repair, split mesh into one file per shell and save to this directory. OUTPUT becomes optional.");

    // ── batch subcommand ──────────────────────────────────────────────────────
    std::vector<fs::path> batch_inputs;
    fs::path              batch_output_dir;
    fs::path              batch_report_path;

    auto* batch_cmd = app.add_subcommand("batch", "Repair multiple mesh files");
    batch_cmd->add_option("FILES", batch_inputs,
        "Input mesh files (.stl .obj .3mf .ply .glb .gltf)")
        ->required()->expected(-1)->check(CLI::ExistingFile);
    batch_cmd->add_option("--output-dir,-d", batch_output_dir,
        "Output directory (default: alongside each input with _repaired suffix)");
    batch_cmd->add_option("--batch-report", batch_report_path,
        "Write per-file repair reports to a JSON array file");

    CLI11_PARSE(app, argc, argv);

    // Apply opts that depend on flags parsed after construction
    opts.fill_holes_smooth        = !flat_fill;
    opts.verbose                  = verbose;
    opts.diagnose_only            = diagnose;
    opts.smooth_crease_angle      = smooth_crease;
    opts.remove_internal_geometry = remove_internal;
    if (remesh_factor > 0.0) {
        opts.remesh                    = true;
        opts.remesh_edge_length_factor = remesh_factor;
        opts.remesh_iterations         = remesh_iters;
    }

    // Build DecimateParams
    DecimateParams decimate_params;
    decimate_params.ratio            = decimate_ratio;
    decimate_params.target_error     = decimate_target_error;
    decimate_params.normal_deviation = decimate_normal_dev;
    if (decimate_backend_str == "meshoptimizer")
        decimate_params.backend = DecimateBackend::MeshOptimizer;
    else if (decimate_backend_str == "openmesh")
        decimate_params.backend = DecimateBackend::OpenMesh;
    else if (decimate_backend_str != "cgal")
        std::cerr << "Warning: unknown --decimate-backend '" << decimate_backend_str
                  << "', falling back to cgal.\n";
    // else: default CGAL

    // Configure logging
    auto logger = spdlog::stdout_color_mt("model-repair");
    if (quiet)
        logger->set_level(spdlog::level::err);
    else if (verbose)
        logger->set_level(spdlog::level::debug);
    else
        logger->set_level(spdlog::level::info);

    // ── dispatch ──────────────────────────────────────────────────────────────
    if (*batch_cmd)
    {
        return run_batch(batch_inputs, batch_output_dir, batch_report_path,
                         opts, ascii_stl, quiet, verbose,
                         decimate_params, smooth_iters, smooth_vulkan,
                         analyze_shells_flag, keep_largest_shell, export_shells_dir,
                         logger);
    }

    // Single-file mode: validate required args that were made optional above
    if (input_path.empty())
    {
        std::cerr << "INPUT is required in single-file mode. Use 'model-repair --help'.\n";
        return 1;
    }
    if (!diagnose && output_path.empty() && export_shells_dir.empty())
    {
        std::cerr << "OUTPUT is required unless --diagnose or --export-shells is used.\n";
        return 1;
    }

    return run_single_file(input_path, output_path, opts, ascii_stl, quiet, verbose,
                           report_path, report_format,
                           decimate_params, smooth_iters, smooth_vulkan,
                           analyze_shells_flag, keep_largest_shell, export_shells_dir,
                           logger);
}

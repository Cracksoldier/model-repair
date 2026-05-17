#include "modelrepair/RepairPipeline.hpp"
#include "modelrepair/RepairReport.hpp"
#include "modelrepair/io/MeshIO.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace modelrepair;

int main(int argc, char* argv[])
{
    CLI::App app{"model-repair: Linux 3D mesh repair tool", "model-repair"};
    app.set_version_flag("--version", "0.1.0");

    // Positional args
    fs::path input_path, output_path;
    app.add_option("INPUT",  input_path,  "Input mesh (.stl, .obj, .3mf)")->required()->check(CLI::ExistingFile);
    app.add_option("OUTPUT", output_path, "Output mesh (.stl, .obj, .3mf)")->required();

    // Repair toggles
    RepairOptions opts;
    app.add_flag("!--no-merge-vertices",            opts.merge_duplicate_vertices,    "Skip duplicate vertex merging");
    app.add_option("--merge-tolerance",             opts.merge_tolerance,             "Vertex merge distance tolerance")->default_val(1e-6);
    app.add_flag("!--no-remove-degenerate",         opts.remove_degenerate_triangles, "Skip degenerate triangle removal");
    app.add_flag("!--no-fix-non-manifold",          opts.fix_non_manifold,            "Skip non-manifold repair");
    app.add_flag("!--no-fix-normals",               opts.fix_normals,                 "Skip normal orientation fix");
    app.add_flag("!--no-fill-holes",                opts.fill_holes,                  "Skip hole filling");
    app.add_option("--max-hole-edges",              opts.max_hole_edges,              "Skip holes with more than N edges (0=all)")->default_val(0);

    bool flat_fill = false;
    app.add_flag("--flat-fill",                     flat_fill,                        "Use flat fan fill instead of smooth hole fill");

    app.add_flag("!--no-remove-self-intersections", opts.remove_self_intersections,   "Skip self-intersection removal (slow)");

    // Output options
    bool ascii_stl = false;
    app.add_flag("--ascii-stl",  ascii_stl, "Write ASCII STL instead of binary");

    std::string report_path;
    app.add_option("--report", report_path, "Write repair report to JSON file");

    std::string report_format = "text";
    app.add_option("--report-format", report_format, "Report format: text or json")->default_val("text");

    bool verbose = false, quiet = false;
    app.add_flag("-v,--verbose", verbose, "Print per-step progress");
    app.add_flag("-q,--quiet",   quiet,   "Suppress all output except errors");

    CLI11_PARSE(app, argc, argv);

    opts.fill_holes_smooth = !flat_fill;
    opts.verbose           = verbose;

    // Configure logging
    auto logger = spdlog::stdout_color_mt("model-repair");
    if (quiet)
        logger->set_level(spdlog::level::err);
    else if (verbose)
        logger->set_level(spdlog::level::debug);
    else
        logger->set_level(spdlog::level::info);

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

    // Save output
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

    logger->info("Done. Manifold: {}, Closed: {}",
                 report.is_manifold_after ? "yes" : "no",
                 report.is_closed_after   ? "yes" : "no");

    return 0;
}

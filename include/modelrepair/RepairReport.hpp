#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace modelrepair
{

struct StepReport
{
    std::string name;
    bool        was_run      = false;
    std::size_t issues_found = 0;
    std::size_t issues_fixed = 0;
    double      duration_ms  = 0.0;
};

struct RepairReport
{
    std::size_t vertices_before  = 0;
    std::size_t triangles_before = 0;
    std::size_t vertices_after   = 0;
    std::size_t triangles_after  = 0;

    bool is_valid_after    = false;
    bool is_manifold_after = false;
    bool is_closed_after   = false;

    std::vector<StepReport> steps;

    std::string format_text() const;
    std::string format_json() const;
};

} // namespace modelrepair

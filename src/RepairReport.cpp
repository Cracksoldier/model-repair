#include "modelrepair/RepairReport.hpp"

#include <sstream>
#include <iomanip>

namespace modelrepair
{

std::string RepairReport::format_text() const
{
    std::ostringstream os;
    os << "=== Repair Report ===\n";
    os << "  Vertices : " << vertices_before  << " -> " << vertices_after  << "\n";
    os << "  Triangles: " << triangles_before << " -> " << triangles_after << "\n";
    os << "  Manifold : " << (is_manifold_after ? "yes" : "no") << "\n";
    os << "  Closed   : " << (is_closed_after   ? "yes" : "no") << "\n";
    os << "\n";
    os << std::left;
    for (const auto& s : steps)
    {
        if (!s.was_run)
            continue;
        os << "  " << std::setw(34) << s.name;
        if (s.issues_found == 0)
            os << "ok";
        else
            os << "found " << s.issues_found << ", fixed " << s.issues_fixed;
        os << "  (" << std::fixed << std::setprecision(1) << s.duration_ms << " ms)\n";
    }
    return os.str();
}

std::string RepairReport::format_json() const
{
    std::ostringstream os;
    os << "{\n";
    os << "  \"vertices_before\": "  << vertices_before  << ",\n";
    os << "  \"vertices_after\": "   << vertices_after   << ",\n";
    os << "  \"triangles_before\": " << triangles_before << ",\n";
    os << "  \"triangles_after\": "  << triangles_after  << ",\n";
    os << "  \"is_manifold_after\": " << (is_manifold_after ? "true" : "false") << ",\n";
    os << "  \"is_closed_after\": "   << (is_closed_after   ? "true" : "false") << ",\n";
    os << "  \"steps\": [\n";
    for (std::size_t i = 0; i < steps.size(); ++i)
    {
        const auto& s = steps[i];
        os << "    {"
           << "\"name\": \"" << s.name << "\", "
           << "\"was_run\": " << (s.was_run ? "true" : "false") << ", "
           << "\"issues_found\": " << s.issues_found << ", "
           << "\"issues_fixed\": " << s.issues_fixed << ", "
           << "\"duration_ms\": " << std::fixed << std::setprecision(2) << s.duration_ms
           << "}";
        if (i + 1 < steps.size())
            os << ",";
        os << "\n";
    }
    os << "  ]\n";
    os << "}\n";
    return os.str();
}

} // namespace modelrepair

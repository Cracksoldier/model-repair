#include "modelrepair/RepairReport.hpp"

#include <sstream>
#include <iomanip>

namespace modelrepair
{

std::string RepairReport::format_text() const
{
    std::ostringstream os;
    os << (diagnose_only ? "=== Diagnosis (no changes saved) ===\n"
                        : "=== Repair Report ===\n");
    os << "  Vertices : " << vertices_before  << " -> " << vertices_after  << "\n";
    os << "  Triangles: " << triangles_before << " -> " << triangles_after << "\n";
    os << std::fixed << std::setprecision(2);
    os << "  Area     : " << surface_area_before << " -> " << surface_area_after << " mm\xc2\xb2\n";
    if (volume_before || volume_after)
        os << "  Volume   : "
           << (volume_before ? std::to_string(*volume_before) : "n/a") << " -> "
           << (volume_after  ? std::to_string(*volume_after)  : "n/a") << " mm\xc2\xb3\n";
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
    os << std::fixed << std::setprecision(6);
    os << "{\n";
    os << "  \"vertices_before\": "        << vertices_before           << ",\n";
    os << "  \"vertices_after\": "         << vertices_after            << ",\n";
    os << "  \"triangles_before\": "       << triangles_before          << ",\n";
    os << "  \"triangles_after\": "        << triangles_after           << ",\n";
    os << "  \"surface_area_before\": "    << surface_area_before       << ",\n";
    os << "  \"surface_area_after\": "     << surface_area_after        << ",\n";
    if (volume_before)
        os << "  \"volume_before\": "      << *volume_before            << ",\n";
    if (volume_after)
        os << "  \"volume_after\": "       << *volume_after             << ",\n";
    os << "  \"is_manifold_after\": "      << (is_manifold_after ? "true" : "false") << ",\n";
    os << "  \"is_closed_after\": "        << (is_closed_after   ? "true" : "false") << ",\n";
    os << "  \"diagnose_only\": "          << (diagnose_only     ? "true" : "false") << ",\n";
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

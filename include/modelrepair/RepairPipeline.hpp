#pragma once

#include "Mesh.hpp"
#include "RepairOptions.hpp"
#include "RepairReport.hpp"
#include <functional>
#include <string>

namespace modelrepair
{

using ProgressCallback = std::function<void(int step, int total, const std::string& step_name)>;

class RepairPipeline
{
public:
    explicit RepairPipeline(RepairOptions opts = {});

    void set_progress_callback(ProgressCallback cb);

    RepairReport run(Mesh& mesh);

private:
    RepairOptions    opts_;
    ProgressCallback progress_cb_;

    StepReport step_merge_vertices(Mesh& mesh);
    StepReport step_remove_degenerate(Mesh& mesh);
    StepReport step_fix_non_manifold(Mesh& mesh);
    StepReport step_fix_normals(Mesh& mesh);
    StepReport step_fill_holes(Mesh& mesh);
    StepReport step_remove_self_intersections(Mesh& mesh);
    StepReport step_remove_internal_geometry(Mesh& mesh);

    void notify(int step, int total, const std::string& name);
};

} // namespace modelrepair

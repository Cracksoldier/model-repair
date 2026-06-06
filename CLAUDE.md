# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**GitHub:** https://github.com/Cracksoldier/model-repair

## Build

```bash
# System deps (Arch/CachyOS)
sudo pacman -S cgal eigen3 boost gmp mpfr qt6-base onetbb vulkan-headers vulkan-icd-loader shaderc

# Configure — downloads lib3mf, CLI11, spdlog, Catch2, tinygltf, meshoptimizer, OpenMesh via FetchContent
cmake --preset debug      # Debug + AddressSanitizer
cmake --preset release    # Release
cmake --preset release-nogui  # Release, no Qt dependency

# Build
cmake --build build/debug -j$(nproc)
```

The build produces three targets: `libmodelrepair.so`, `model-repair` (CLI), and `model-repair-gui` (Qt).

## Tests

```bash
# Run all tests (ASan requires LD_PRELOAD in debug)
LD_PRELOAD=$(gcc -print-file-name=libasan.so) ctest --preset debug --output-on-failure

# Run a single test by name
LD_PRELOAD=$(gcc -print-file-name=libasan.so) \
  build/debug/tests/model-repair-tests "Fill holes closes an open surface"

# Run tests matching a tag
LD_PRELOAD=$(gcc -print-file-name=libasan.so) \
  build/debug/tests/model-repair-tests "[pipeline]"

# Release — no LD_PRELOAD needed
ctest --preset release --output-on-failure
```

The `Test_CPP_Bindings` test (lib3mf's own suite) is excluded by the test presets — it is not our code.

## Run

```bash
# CLI — needs the shared lib on the path
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair INPUT.stl OUTPUT.stl --verbose

# CLI — diagnose only (no OUTPUT required; mesh is not modified)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair broken.stl --diagnose --verbose

# CLI — repair then decimate to 50 % of faces
# Saves broken_repaired.stl (intermediate, post-smooth) and out.stl (decimated)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair broken.stl out.stl --decimate 0.5

# CLI — smooth blocky mesh (5 iterations, feature-preserving cotangent Laplacian)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair blocky.stl smooth.stl --smooth 5

# CLI — smooth then decimate (smooth runs first, intermediate is post-smooth)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair blocky.stl out.stl --smooth 3 --decimate 0.5

# CLI — GPU-accelerated smoothing (falls back to CPU if Vulkan unavailable)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair blocky.stl smooth.stl --smooth 5 --smooth-vulkan

# CLI — enable pipeline step 7 (remove faces hidden inside the mesh)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair broken.stl out.stl --remove-internal-geometry

# CLI — decimate with MeshOptimizer backend (fast, error-bounded)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair broken.stl out.stl --decimate 0.5 \
  --decimate-backend meshoptimizer --decimate-target-error 0.01

# CLI — decimate with OpenMesh QEM backend
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair broken.stl out.stl --decimate 0.5 \
  --decimate-backend openmesh --decimate-normal-dev 15

# CLI — analyse connected shells (works with --diagnose too;
#        in diagnose mode output is labelled "(pre-repair, original mesh)")
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair multi.stl --diagnose --analyze-shells

# CLI — keep only the largest shell after repair
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair multi.stl out.stl --keep-largest-shell

# CLI — export every shell as a separate file (OUTPUT is optional;
#        read-only, so works with --diagnose too)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair multi.stl --export-shells ./shells/

# CLI — export all shells AND keep only the largest in the combined output
#        (shells are exported first from the full mesh, then the largest is kept)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair multi.stl out.stl \
  --keep-largest-shell --export-shells ./shells/

# GUI
build/debug/gui/model-repair-gui
```

## Docs

The public website lives in `docs/`:
- `docs/index.html` — landing page (features, setup tabs, download button)
- `docs/documentation.html` — full API and architecture reference

Both are self-contained HTML files (no build step). The site is served via GitHub Pages from the `docs/` folder on `main`. All GitHub links use the slug `Cracksoldier/model-repair`.

## Release

Releases are built by two manually triggered GitHub Actions workflows — one for Linux (AppImage) and one for Windows (ZIP).

### Workflow: `.github/workflows/appimage.yml` (Linux)

Trigger via **Actions → Build AppImage → Run workflow** with three inputs:

| Input | Example | Description |
|---|---|---|
| `version` | `v0.1.0` | Git tag and artifact label for the release |
| `create_release` | `true` | Create a draft GitHub Release with the AppImage attached |
| `next_version` | `0.2.0` | Version to write into `CMakeLists.txt` after the release |

**Workflow steps:**
1. Builds inside `archlinux:latest` (required — CGAL 6.x is only available on Arch)
2. `cmake --install --component Runtime` into `AppDir/usr/`
3. Copies `libmodelrepair.so*` and `lib3mf.so*` into `/usr/lib` so `ldd` resolves them normally
4. Downloads and runs `linuxdeploy` + `linuxdeploy-plugin-qt` to bundle all Qt and system dependencies
5. Uploads the AppImage as a workflow artifact
6. If `create_release=true`: creates a draft GitHub Release and pushes a git tag (job requires `permissions: contents: write`)
7. If `next_version` is also set: bumps `project(... VERSION ...)` in `CMakeLists.txt` and pushes the commit to `main`

**Key implementation constraints:**
- `APPIMAGE_EXTRACT_AND_RUN=1` — linuxdeploy is itself an AppImage; this bypasses FUSE (unavailable in Docker containers)
- `NO_STRIP=1` — linuxdeploy's bundled `strip` binary predates the `.relr.dyn` ELF section used by modern Arch packages and would crash
- Custom libs are copied to `/usr/lib` rather than using `LD_LIBRARY_PATH`, because `LD_LIBRARY_PATH=AppDir/usr/lib` causes `qmake6` to load the rpath-patched Qt libs from AppDir and report wrong `QT_INSTALL_LIBS`, breaking `linuxdeploy-plugin-qt`
- Version bump step runs `git config --global --add safe.directory "${GITHUB_WORKSPACE}"` first — Git 2.35.2+ rejects repos owned by a different UID (root vs runner) without this
- The release is created as a **draft** — review and publish it manually on GitHub

### Workflow: `.github/workflows/windows.yml` (Windows)

Trigger via **Actions → Build Windows → Run workflow** with the same three inputs (`version`, `create_release`, `next_version`).

**Workflow steps:**
1. Sets up MSYS2 UCRT64 and installs all dependencies as prebuilt MSYS2 packages (CGAL, Eigen3, Qt6, GMP, MPFR, Boost, Ninja, GCC)
2. Configures with `cmake -B build -G Ninja` (no preset — tests and ASan disabled)
3. Builds with `cmake --build build`
4. Bundles into `dist/model-repair/`: uses `find` to locate EXEs and `libmodelrepair.dll` (paths vary depending on `qt6_standard_project_setup()`), copies `lib3mf.dll`, runs `windeployqt6` for Qt DLLs, then scans all binaries with `ldd` to copy MSYS2 UCRT64 runtime DLLs
5. Creates a ZIP with PowerShell `Compress-Archive`
6. Uploads the ZIP as a workflow artifact; optionally creates a GitHub Release and bumps the version

**Key implementation constraints:**
- MSYS2 UCRT64 ships Eigen3 5.x; `find_package(Eigen3)` must not specify a version number (no `3.4` minimum) — `CMakeLists.txt` uses `find_package(Eigen3 REQUIRED NO_MODULE)` without a version
- `qt6_standard_project_setup()` sets `CMAKE_RUNTIME_OUTPUT_DIRECTORY` to the top-level build directory, so all EXEs and the `libmodelrepair.dll` land in `build/` rather than `build/gui/`, `build/cli/`, `build/src/` — the bundle step uses `find` rather than hardcoded paths for this reason
- lib3mf's bundled libzip has Windows-only sources (`zip_source_file_win32_ansi.c`, `zip_source_file_win32_utf16.c`) that assign Win32 function pointers into `void*` struct fields; GCC 16 treats `-Wincompatible-pointer-types` as an error — suppressed via `#pragma GCC diagnostic` injected by `cmake/patch_lib3mf_gcc16.cmake`
- The CMake preset `windows-release` in `CMakePresets.json` mirrors what the workflow does and can be used for local Windows builds

## Architecture

The codebase is split into a shared library, two frontends, and a test suite.

### `libmodelrepair` (`src/`, `include/modelrepair/`)

The public API is three types plus two free functions:
- **`RepairOptions`** — plain struct of booleans/values, one per pipeline step. Additional fields: `diagnose_only` (run detection without modifying the mesh), `remove_internal_geometry` (step 7 — centroid-inside test, `false` by default), `remesh` / `remesh_edge_length_factor` / `remesh_iterations` (pre-smooth isotropic remeshing, experimental), `smooth` / `smooth_iterations` / `smooth_crease_angle` (post-repair feature-preserving smoothing, experimental), `decimate` / `decimate_ratio` / `decimate_backend` / `decimate_target_error` / `decimate_normal_dev` (post-repair polygon-count reduction — backend selects CGAL/MeshOptimizer/OpenMesh).
- **`RepairPipeline`** — call `run(Mesh&)`, optionally set a `ProgressCallback` first. When `opts.diagnose_only` is true, `run()` deep-copies the mesh, runs all steps on the copy, and returns a report without touching the original.
- **`RepairReport`** — returned by `run()`, contains a `StepReport` per step plus final mesh stats. Fields added: `surface_area_before/after` (mm², always populated), `volume_before/after` (mm³, `std::optional` — nullopt when the mesh is not closed), `diagnose_only`.
- **`analyze_wall_thickness(const Mesh&)`** (`include/modelrepair/WallThickness.hpp`, `src/analysis/WallThickness.cpp`) — free function; builds a `CGAL::AABB_tree` over all faces, shoots an inward ray from centroid+ε along −normal per face, extracts the first-intersection distance via `std::visit` on the `std::variant<Point3, Segment_3>` result. Returns `std::vector<double>` of per-face thicknesses (one per face in iteration order; `numeric_limits<double>::max()` when no hit). Used by `PreviewWindow` to populate the Wall Thickness heatmap.
- **`analyze_shells(const Mesh&)`** / **`keep_shells(Mesh&, size_t keep_n=1)`** / **`split_shells(const Mesh&)`** (`include/modelrepair/ShellSeparation.hpp`, `src/repairs/ShellSeparation.cpp`) — free functions; use `boost::associative_property_map` + `PMP::connected_components` on the const mesh to build a face→component-id map without mutation. `analyze_shells` counts faces per component, detects open components by scanning border halfedges, sorts largest-first, and returns `ShellSeparationResult{components_found, shells[]}`. `keep_shells` calls `PMP::keep_largest_connected_components`. `split_shells` builds a polygon-soup per component (using a `v_remap` unordered_map), calls `orient_polygon_soup` + `polygon_soup_to_polygon_mesh`, and returns one `Mesh` per component sorted largest-first.
- **`remove_internal_geometry(Mesh&)`** (`include/modelrepair/RemoveInternalGeometry.hpp`, `src/repairs/RemoveInternalGeometry.cpp`) — free function; returns early if mesh is not closed; builds `CGAL::Side_of_triangle_mesh<SurfMesh, Kernel>`, computes `CGAL::centroid(p0,p1,p2)` per face, collects faces where `inside_test(centroid) == ON_BOUNDED_SIDE`, calls `sm.remove_face(f)` + `sm.collect_garbage()`. Returns `RemoveInternalGeometryResult{faces_before, faces_removed, faces_after, duration_ms}`. Also exposed as pipeline step 7 via `src/repairs/RemoveInternalGeometryStep.cpp`.
- **`subdivide(Mesh&, unsigned int iterations=1, SubdivisionMethod method=Loop)`** (`include/modelrepair/Subdivide.hpp`, `src/repairs/Subdivide.cpp`) — free function; calls `CGAL::Subdivision_method_3::Loop_subdivision` or `CatmullClark_subdivision` one iteration at a time so UV coordinates can be propagated to new vertices after each pass. New vertices inherit UV by averaging from old 1-ring neighbours (pass 1); Catmull-Clark face-centroid vertices (adjacent only to other new edge-midpoints) get a second pass averaging from all neighbours after pass 1 has set edge-midpoint UVs. Returns `SubdivisionResult{faces_before, faces_after, duration_ms}`. Used from `WizardWorker` Phase 2.
- **`displace_from_normal_map(Mesh&, const NormalMapDisplaceParams&)`** (`include/modelrepair/NormalMapDisplace.hpp`, `src/repairs/NormalMapDisplace.cpp`) — free function; throws `std::runtime_error` if the mesh has no UV map or the image cannot be loaded. Steps: (1) subdivide `pre_subdivisions` times (Loop) to increase vertex density; (2) load the normal map with `stb_image` (sourced from tinygltf's FetchContent tree; `STB_IMAGE_IMPLEMENTATION` + `STB_IMAGE_STATIC` defined in this TU only); (3) for each vertex, sample the blue/Z channel at its UV coordinate, decode to `[-1,1]`, compute `pseudo_height = (1 - nz) * 0.5` (flat nz=1 → 0, tilted nz=-1 → 1), displace along the geometric vertex normal by `pseudo_height * displacement_strength`. Optional `flip_green` inverts the G channel (DirectX normal maps); `flip_y_uv` flips V before sampling (image Y=0 is top, UV Y=0 is bottom). Returns `NormalMapDisplaceResult{faces_before, faces_after, duration_ms}`. Used from `WizardWorker` Phase 2 (highly experimental — marked as such in the UI).
- **`normal_to_displacement(const std::string& path, const NormalToDisplacementSettings&)`** (`include/modelrepair/NormalToDisplacement.hpp`, `src/analysis/NormalToDisplacement.cpp`) — free function; pure image-in / image-out Poisson reconstruction of a height map from a tangent-space normal map. No mesh required. Steps: (1) load with `stb_image` (`STB_IMAGE_IMPLEMENTATION` + `STB_IMAGE_STATIC` defined in this TU); raw buffer is freed via RAII (`StbiDeleter` unique_ptr) so it is released even if subsequent Eigen allocation throws; (2) decode tangent normals (optional `flip_green`), normalize, clamp `nz` to 1e-4 to avoid division by zero, build gradient field `gx/gy = -nx_v/nz_v * gradient_strength` clamped to ±20; (3) compute backward-difference divergence `div = dgx/dx + dgy/dy`, negate to form the right-hand side; (4) build an `Eigen::SparseMatrix<float>` with a 5-point Laplacian using Neumann reflection at image edges and Dirichlet BC at pixel 0 (row 0 = identity, `b[0] = 0` to remove the constant-mode singularity; the `add_off` lambda always increments the diagonal count before the Dirichlet skip so the true graph degree is preserved); (5) solve with `Eigen::ConjugateGradient` + `DiagonalPreconditioner` (`solver_max_iter`, tolerance 1e-5); (6) optional box blur, normalize to [0,1], apply contrast, optionally invert. Hard limit: 8,000,000 pixels (≈2828×2828); throws with a clear message if exceeded. Returns `NormalToDisplacementResult{height_vec, width, height_px, duration_ms}`. Used by `NormalToDisplacementWindow` in the GUI's **Tools** dropdown.
- **`smooth(Mesh&, unsigned int iterations, double crease_angle = 45.0, std::function<void(unsigned int)> on_iteration = nullptr)`** (`include/modelrepair/Smooth.hpp`, `src/repairs/Smooth.cpp`) — free function; cotangent-weighted feature-preserving Laplacian (λ=0.5): each interior vertex moves toward a weighted average of its 1-ring neighbours; each neighbour weight is cot(α)+cot(β) (Laplace-Beltrami). Each weight is additionally scaled by a soft linear falloff on the shared edge's dihedral angle — flat edges at full weight (1.0), edges at the crease threshold at zero, linearly interpolated in between; edges sharper than `crease_angle` are fully excluded, acting as smoothing barriers. After all iterations, volume is restored via uniform scaling around the centroid (closed meshes only; ~2–4% shrinkage otherwise). No `angle_and_area_smoothing` pass. Pure EPICK. `on_iteration` fires after each completed iteration with its 1-based index (same pattern as `remesh()`). Returns `SmoothResult{duration_ms}`.
- **`remesh(Mesh&, double edge_length_factor = 0.8, unsigned int iterations = 3)`** (`include/modelrepair/Remesh.hpp`, `src/repairs/Remesh.cpp`) — free function; computes mean edge length, scales by `edge_length_factor` to get `target_len`, marks all edges ≤ `target_len` as constrained via `edge_is_constrained_map + protect_constraints(true)`, then calls `PMP::isotropic_remeshing` on the full face range. Fine-geometry edges (face, hands, fingers) are frozen; coarse long edges are freely split. Passing the full mesh (rather than a face subset) avoids the `is_on_patch_border` assertion that fires with pinch-point patch topologies. Returns `RemeshResult{faces_before, faces_after, duration_ms}`.
- **`decimate(Mesh&, double ratio)`** / **`decimate(Mesh&, const DecimateParams&)`** (`include/modelrepair/Decimate.hpp`, `src/repairs/Decimate.cpp`) — free functions; reduce face count to `ratio` fraction using one of three backends selected by `DecimateParams::backend` (`DecimateBackend` enum: `CGAL`, `MeshOptimizer`, `OpenMesh`). CGAL backend uses `Surface_mesh_simplification::edge_collapse()` with `Face_count_ratio_stop_predicate` (in-place, no polygon-soup rebuild, preserves all property maps natively). MeshOptimizer backend calls `meshopt_simplify()` (fast, error-bounded; `target_error` field — default 0.01); rebuilds from polygon soup, saving/restoring `"v:color"` by position-index vector. OpenMesh backend converts to `TriMesh_ArrayKernelT<>`, uses `DecimaterT` + `ModQuadricT` (QEM) with optional `ModNormalDeviationT` (enabled when `normal_deviation < 179.0`); rebuilds from polygon soup, saving/restoring `"v:color"` by OM handle index. `duration_ms` covers the full operation including conversions. Returns `DecimateResult{faces_before, faces_after, duration_ms, backend_used}`. Backend availability probes: `decimate_meshoptimizer_available()` and `decimate_openmesh_available()` return compile-time booleans; passing an unavailable backend silently falls back to CGAL.

`Mesh` is a thin wrapper around `CGAL::Surface_mesh<EPICK::Point_3>`. All repair steps work directly on `mesh.cgal()`. New methods: `surface_area()` → `double` and `volume()` → `std::optional<double>` (wraps `PMP::area` / `PMP::volume`; volume returns nullopt when the mesh is not closed); `has_uv()` → `bool` (returns true when the mesh carries a `"v:uv"` per-vertex property map of type `UV2 = std::array<float,2>`).

### Repair pipeline step ordering (`src/repairs/`)

Each step file implements one private method of `RepairPipeline`. The order is a hard dependency chain — each step is a precondition for the next:

1. **MergeDuplicateVertices** — converts to polygon soup, calls `PMP::repair_polygon_soup` (which merges coincident points and removes degenerate/duplicate polygons), re-orients, rebuilds `Surface_mesh`. STL files always arrive as soup with no vertex sharing, so this step always reports merges.
2. **RemoveDegenerateTriangles** — `PMP::remove_degenerate_faces`. Zero-area faces left after step 1.
3. **FixNonManifold** — `PMP::duplicate_non_manifold_vertices` (outputs `halfedge_descriptor`, not `vertex_descriptor`).
4. **FixNormals** — `PMP::orient`. Skips `is_outward_oriented` check when the mesh is not closed (that function asserts `is_closed`).
5. **FillHoles** — `PMP::triangulate_and_refine_hole` per boundary cycle. Collects cycles via a visited-set traversal of border halfedges.
6. **RemoveSelfIntersections** — the only step that uses EPECK. Converts to `Surface_mesh<EPECK::Point_3>` via `CGAL::copy_face_graph`, calls `PMP::experimental::remove_self_intersections`, converts back. Slowest step.
7. **RemoveInternalGeometry** — builds `CGAL::Side_of_triangle_mesh<SurfMesh, Kernel>` on the closed mesh, computes `CGAL::centroid(p0,p1,p2)` per face, removes faces where the centroid is `ON_BOUNDED_SIDE`, then calls `collect_garbage()`. Returns early if the mesh is not closed. Disabled by default (`remove_internal_geometry = false`).

### I/O (`src/io/`, `include/modelrepair/io/`)

`MeshIO.{hpp,cpp}` is the format-agnostic entry point (`load`/`save`, format inferred from extension). Each format calls CGAL polygon-soup helpers (`orient_polygon_soup` → `polygon_soup_to_polygon_mesh`) after reading raw geometry. Supported extensions: `.stl`, `.obj`, `.3mf`, `.glb`, `.gltf`, `.ply`.

Per-vertex RGB color is stored as a `CGAL::IO::Color` property map on the mesh under the key `"v:color"` (`SurfMesh::Vertex_index` → `CGAL::IO::Color`). Readers attach this map when color data is present; writers emit it when the map exists. The map is transparent to repair steps — vertices that survive structurally keep their color. Steps that rebuild the mesh from polygon soup explicitly save and restore the map: Step 1 (MergeVertices) saves to a `position→color` hash table; the MeshOptimizer decimation backend saves by position-index vector (built during `v_remap` construction, restored in `pts` order after `polygon_soup_to_polygon_mesh`); the OpenMesh decimation backend saves by OM handle index (built during CGAL→OMesh conversion, restored in `pts` order). In all three cases `polygon_soup_to_polygon_mesh` adds vertices in `pts`-array order for a clean manifold soup, so `new_sm.vertex(j)` maps to `pts[j]`. Colors are preserved exactly when no vertices are actually merged or dropped.

Color attachment guards: all three color-capable readers snapshot the point count before calling `orient_polygon_soup` (which may append points for non-manifold vertex fans) and use the pre-orient count in the size equality check, preventing a stale match from silently assigning colors to wrong vertices.

- `ThreeMFIO.cpp` wraps `lib3mf`; its headers live in `build/<preset>/_deps/lib3mf-src/Autogenerated/Bindings/Cpp/` and are added as a PRIVATE include in `src/CMakeLists.txt`.
- `GlbIO.cpp` wraps `tinygltf` (header-only, fetched via FetchContent). `TINYGLTF_HEADER_ONLY=ON` is required — the default compiled-library mode produces non-PIC code that cannot link into the shared `.so`. `TINYGLTF_IMPLEMENTATION` is defined in `GlbIO.cpp` only, with stb_image disabled (`TINYGLTF_NO_STB_IMAGE` / `TINYGLTF_NO_INCLUDE_STB_IMAGE`). Both `.glb` (binary) and `.gltf` (text) are loadable; save always writes binary GLB. Colors are read from the `COLOR_0` accessor (VEC3/VEC4, FLOAT/UNSIGNED_BYTE/UNSIGNED_SHORT); written as VEC3 FLOAT. The `"v:color"` map is only attached when every primitive in the file has `COLOR_0` — a file mixing colored and uncolored primitives silently drops colors to avoid painting uncolored vertices black.
- `PlyIO.cpp` uses `CGAL::IO::read_PLY` (from `<CGAL/IO/PLY.h>`, no new dependency) which handles ASCII, binary LE, and binary BE automatically. Write is manual (~80 lines of binary LE PLY): the header conditionally includes `red/green/blue uchar` properties only when `"v:color"` is present, giving full control over optional color output. OBJ read supports the `v x y z r g b` extension (Meshlab/CloudCompare); the `"v:color"` map is only attached when every vertex line has rgb (a file mixing colored and color-less vertex lines silently drops colors). OBJ read also parses `vt u v` lines and the `/vt` slot of `f v/vt/vn` face entries; per-corner UVs are averaged into a per-vertex `"v:uv"` property map (`UV2 = std::array<float,2>`). The UV accumulation happens before `orient_polygon_soup` so that `face_uv_indices[fi][ci]` still corresponds to `polygons[fi][ci]`. The map is attached only when every face corner had a UV index and the final vertex count equals `n_points_before_orient` (same guard as vertex colors).

### GUI (`gui/`)

`RepairWorker` (QObject) runs on a `QThread`. It accepts a `std::shared_ptr<std::atomic<bool>> cancel_flag` (created by `MainWindow::start_worker` and shared with the main thread). It captures a copy of the loaded mesh before calling `RepairPipeline::run` (which mutates the mesh in-place), then emits either a four-argument `finished` signal (carrying the `RepairReport`, before-mesh, after-mesh, and error string) or a two-argument `cancelled` signal (carrying the partial mesh and the count of completed pipeline steps, 0–7). Progress signals are emitted directly from the worker thread; Qt's auto-connection detects the cross-thread receiver and uses `Qt::QueuedConnection` automatically (posting to the main event loop). A `grand_total` is computed upfront — 7 (pipeline steps) + `remesh_iterations` (if remesh enabled) + `smooth_iterations` (if smooth enabled) + 1 (if decimate enabled) — so the progress bar covers all operations in one unified step count, advancing one slot per iteration. Cancellation is checked at three points: (1) in the pipeline's `ProgressCallback` — fired before each step, so throwing a local `CancelledException` (not derived from `std::exception`) stops the pipeline between steps, with `steps_completed = step - 1`; (2) immediately after `pipeline.run()` returns (catches a cancel requested during the last — and slowest — step); (3) in the per-iteration callbacks of `remesh()` and `smooth()`, which fire after each completed iteration, leaving the mesh in a clean state. After `pipeline.run()`, post-repair operations run in order: if `opts_.remesh` is true and not diagnose-only, `RepairWorker` calls `remesh()` with a per-iteration callback and advances the bar through "Remeshing 1/N" … "Remeshing N/N"; then if `opts_.smooth` is true, calls `smooth()` with a per-iteration callback and advances through "Smoothing 1/N" … "Smoothing N/N"; then if `opts_.decimate` is true, calls `decimate()`, appends a `StepReport`, and updates `report.triangles_after`, `surface_area_after`, and `volume_after`. Each post-repair block also checks `cancel_flag` at its entry point.

`MainWindow` wires everything together; `ReportView` (QTreeWidget) renders the `RepairReport`. A **Tools** toolbar (`QToolBar`, non-movable) sits at the top of the window; it contains a single `QToolButton` with `InstantPopup` mode and a `QMenu`. The menu currently has one entry: **"Normal Map → Displacement"**, which opens `NormalToDisplacementWindow` (singleton-tracked via `nm_disp_win_` member, same pattern as `wizard_`). The report's summary row shows vertex/triangle counts plus surface area and volume (when the mesh is closed). On success, `MainWindow` opens a `PreviewWindow` (non-modal, `WA_DeleteOnClose`). A **"Diagnose"** button runs the pipeline with `diagnose_only = true`; the result is shown in the report tree but `btn_save_` is disabled and no preview opens. A **"Cancel"** button (enabled while a repair is running, disabled otherwise) stores `true` into the shared `cancel_flag_` and immediately greys itself out; `on_repair_cancelled` is invoked when the worker stops early: if at least one pipeline step completed it shows a `QMessageBox::question` offering to save the partial result — "Yes" populates `repaired_mesh_` and enables `btn_save_`; "No" discards it. The `cancel_flag_` (`std::shared_ptr<std::atomic<bool>>`) is allocated in `start_worker()`, shared with the `RepairWorker`, and reset to `nullptr` in both `on_repair_finished` and `on_repair_cancelled`. A **"Remesh (isotropic) before smoothing [experimental]"** checkbox + edge-length-factor spinbox (0.1–2.0, default 0.8) + iterations spinbox (1–10, default 3) controls pre-smooth isotropic remeshing. A **"Smooth after repair [experimental]"** checkbox + iterations spinbox (1–50, default 3) + crease-angle spinbox (0–180°, default 45°) controls post-repair feature-preserving smoothing. A **"Decimate after repair"** checkbox + ratio spinbox (0.01–1.00, default 0.50) + backend `QComboBox` (`combo_decimate_backend_`) controls post-repair decimation. The combo lists CGAL (always available), MeshOptimizer (disabled in combo when not compiled in), and OpenMesh (disabled when not compiled in), initialized from `initial_opts.decimate_backend`. An info `QLabel` (`lbl_decimate_info_`) below the combo describes the selected backend. A collapsible `QWidget` (`decimate_extra_params_`) holds backend-specific parameters: `spin_decimate_target_error_` (MeshOptimizer max error, default 0.01) and `spin_decimate_normal_dev_` (OpenMesh normal deviation limit in degrees, default 15.0); the widget is shown only when MeshOptimizer or OpenMesh is selected. `collect_options()` sets `o.decimate_backend`, `o.decimate_target_error`, and `o.decimate_normal_dev` from these widgets. A **"Remove internal geometry"** checkbox (`chk_remove_internal_`) enables pipeline step 7 (off by default). A **"Separate Shells…"** button (`btn_shells_`, enabled when a mesh is loaded) opens an inline dialog showing the number of detected connected components; it offers "Keep Largest" (calls `keep_shells(mesh, 1)`) and "Export All…" (calls `split_shells()` and saves each shell with `io::save()` to a user-chosen directory). Remesh, smooth, and decimate are all disabled in diagnose mode. `set_busy()` disables the entire options `QGroupBox` (`opts_group_`, cascading to all child widgets), `btn_open_`, `btn_batch_`, and `drop_label_` while a repair is running, and enables `btn_cancel_`. The cancel partial-result question fires when `pipeline_steps_completed < 7` (the pipeline now has 7 steps). An `QElapsedTimer elapsed_clock_` (total phase time) and `QElapsedTimer task_clock_` (current step time) are started at the beginning of each repair; a `QTimer* elapsed_timer_` fires every second to update `elapsed_label_` (right-aligned next to `status_label_`) in `M:SS / M:SS` format (total / step), giving the user a live indication that work is in progress. `task_clock_` resets on each `on_progress()` call so the right-hand figure shows time for the current step only. The final elapsed time remains visible after repair completes. A **"Batch Repair…"** button opens a `BatchWindow` (non-modal, `WA_DeleteOnClose`) pre-populated with the current repair options.

`BatchWindow` processes a list of files sequentially on a background `QThread` (one `RepairWorker` per file, reused). Files are added via drag-and-drop or "Add Files…". Output defaults to the same directory as the input with a `_repaired` filename suffix; an alternative output directory can be set with "Browse…". A table shows Filename / Status / Issues / Watertight / Time per file. "Preview Selected" opens a `PreviewWindow` for the selected completed job. The window cannot be closed while a repair is running (close event is ignored; click Cancel first). The options panel mirrors all `MainWindow` repair controls including the decimation backend selector (`combo_decimate_backend_` + extra params widget), pre-populated from the `RepairOptions` passed to the constructor; `collect_options()` reads all fields including `decimate_backend`, `decimate_target_error`, and `decimate_normal_dev`.

`PreviewWindow` shows a side-by-side Before/After 3D comparison. It owns copies of both meshes as value members. Each `MeshViewWidget` (QOpenGLWidget) renders one mesh using OpenGL 3.3 with flat-shaded Phong lighting (GLSL 3.30, flat triangle VBO with interleaved `[pos, normal, scalar]` — 7 floats per vertex, stride 28 bytes). The scalar slot (attribute location 2) feeds a heatmap shader: `uniform int u_display_mode` (0 = Normal, 1 = Heatmap); when mode is 1, `vec3 heat = mix(vec3(0.0,0.4,1.0), vec3(1.0,0.1,0.0), clamp(v_scalar,0.0,1.0))` replaces the solid color (`blue = thin/flat, red = thick/steep`). `MeshViewWidget` exposes `set_display_mode(DisplayMode, std::vector<float> per_face_scalars)` and a `DisplayMode` enum (`Normal`, `WallThickness`, `Overhang`). For `WallThickness`, `upload_mesh()` maps per-face values from `per_face_scalars_` to the scalar slot (thin faces → 1.0 = red; cap at 95th percentile for normalization). For `Overhang`, the scalar is computed inline as `std::max(0.0f, -nz)` from the face normal's Z component (horizontal faces → 0 = blue; steep overhangs → 1 = red). `PreviewWindow` adds a `QComboBox* mode_combo_` above the viewer pair with items "Normal", "Wall Thickness", "Overhang"; selecting "Wall Thickness" calls `analyze_wall_thickness(after_mesh)` and normalizes results to [0,1] before calling `set_display_mode(WallThickness, scalars)` on the after-widget; "Overhang" calls `set_display_mode(Overhang)` on both widgets. Both widgets share a single `std::shared_ptr<CameraState>` (rotation as QQuaternion, zoom, pan, scene_radius). When any camera-mutating mouse event fires the emitting widget calls `emit camera_changed()`; the peer is connected to that signal and calls `update()` — no mutex needed because both live in the main thread. Mouse controls: left-drag = arcball rotation, right-drag = XY pan, scroll = zoom.

`WizardWindow` (`gui/WizardWindow.hpp/cpp`) is a `QDialog` implementing a three-phase guided repair workflow, opened via the **Wizard…** button (enabled when a file is loaded, disabled during repair). It contains a `QStackedWidget` with three `QWidget` pages (`Phase1Page`, `Phase2Page`, `Phase3Page`); each page contains an inner `QStackedWidget` cycling through options/running/preview states. The wizard keeps a `current_mesh_` updated after each phase and a `phase_start_mesh_` copy (set immediately before each worker launch) for the before-preview and for retry reversion. `WizardWindow::closeEvent` blocks close while a background worker is running (same pattern as `BatchWindow`). Only one wizard instance can be open at a time — `MainWindow` tracks it via `WizardWindow* wizard_` and raises the existing window if the button is clicked again; the pointer is nulled via `QObject::destroyed`. Phase 1 runs the repair pipeline (all 7 steps, no post-processing); Phase 2 runs isotropic remesh and/or feature-preserving smooth and/or subdivision; Phase 3 runs decimation. Each phase shows a before/after `MeshViewWidget` pair with a shared `CameraState` in the preview state. "Save As…" buttons in each phase's preview call `try_save_and_accept()`, which opens a `QFileDialog`, calls `modelrepair::io::save()`, and calls `accept()` only on success (or returns without closing if the dialog is cancelled or save fails). Phase 2 and Phase 3 previews also show a **"← Adjust settings"** button: clicking it calls `on_phase2_retry()` / `on_phase3_retry()`, which reverts `current_mesh_ = phase_start_mesh_` and returns to the options page (Phase 3 additionally refreshes its analysis labels via `page3_->show_analysis(current_mesh_)`). The spinbox values are preserved across retry so the user can tweak parameters without resetting to defaults. `populate_preview_area()` (a file-scope static) deletes old `MeshViewWidget` children and layout before constructing fresh ones, so retry + rerun never accumulates duplicate GL widgets. The **Cancel** button remains enabled throughout all phases. During Phase 2 (RemeshSmooth) it sets a shared `cancel_flag_` (`std::shared_ptr<std::atomic<bool>>`), which `WizardWorker` checks after each remesh/smooth iteration, interrupting the operation after the current iteration completes and returning silently to the options page; during Phase 1 and Phase 3 the flag is set but those phases are non-interruptible so they run to completion. Each phase's running state shows a live `M:SS / M:SS` elapsed-time label (total phase time / time since last step started), initialized to `"0:00 / 0:00"` immediately when the phase starts; the timer is driven by `elapsed_clock_` + `step_clock_` + `elapsed_timer_` (1 s interval) owned by `WizardWindow`, with `step_clock_.restart()` called in `on_progress()`. Phase 2 additionally shows a warning when the edge length factor is ≤ 0.6 (very fine mesh, runtimes can reach hours), and suppresses the large-mesh warning when the fine-factor warning is already active to avoid stacking. Phase 2 also has a **Subdivide** accordion section with `chk_subdivide_`, `combo_subdiv_method_` ("Loop (triangles)" / "Catmull-Clark (general)"), `spin_subdiv_iters_` (1–4, default 1), and a `warn_subdiv_` label that appears when `face_count * 4^iters > 2,000,000`. Phase 2 also has a fourth **"Normal Map Displacement [highly experimental]"** accordion (after Subdivide): `chk_displace_` (off by default, disabled when mesh has no UV map), `warn_displace_no_uv_` (shown instead when mesh lacks UVs), `edit_normal_map_` + Browse button (opens `QFileDialog` for PNG/JPG/TGA), `spin_displace_strength_` (0.01–5.0 mm, default 0.3), `spin_displace_presubdiv_` (0–4, default 2), `chk_flip_green_`. `set_has_uv(bool)` enables/disables the accordion based on whether the loaded mesh carries UV data. `enter_phase2()` calls `page2_->set_has_uv(current_mesh_.has_uv())`.

`WizardWorker` (`gui/WizardWorker.hpp/cpp`) is a `QObject` with three constructors (one per phase) that capture all parameters needed for that phase. A single `run()` slot dispatches on the phase enum. Signals: `progressChanged(int step, int total, QString name)` and `finished(Mesh result, RepairReport report, QString error)`. The per-iteration callbacks in the remesh and smooth branches always emit (no `completed < n` guard), so the progress bar advances on the final iteration rather than stalling. Phase 2 total = `(do_remesh ? remesh_iters : 0) + (do_smooth ? smooth_iters : 0) + (do_subdivide ? 1 : 0) + (do_displace ? 1 : 0)`. The subdivision branch calls `modelrepair::subdivide(mesh_, subdivide_iters_, method)` after smooth, checking `cancel_flag_` before entering (throws `WizardCancelled` if set). The displacement branch runs after subdivision: checks `cancel_flag_`, emits `progressChanged`, builds a `NormalMapDisplaceParams` from the wizard parameters, calls `displace_from_normal_map(mesh_, dp)`, and appends a `StepReport`. A `cancel_flag_` (`std::shared_ptr<std::atomic<bool>>`) is set via `set_cancel_flag()` by `WizardWindow` before the thread starts; Phase 2 (RemeshSmooth) checks it at the start of each iteration callback and throws the anonymous-namespace struct `WizardCancelled` (not derived from `std::exception`, so it bypasses the inner `catch (const std::exception&)` blocks) when the flag is set; the outer catch emits `finished(..., "__cancelled__")`. `WizardWindow::on_worker_finished` detects this sentinel and returns silently to the options page.

`NormalToDisplacementWindow` (`gui/NormalToDisplacementWindow.hpp/cpp`) is a non-modal `QWidget` (singleton-tracked in `MainWindow` via `nm_disp_win_`, same pattern as `wizard_`) opened from the **Tools** dropdown toolbar. Left panel: image path + Browse, settings (flip green, invert, gradient strength, contrast, blur radius, solver iterations, normalize), Run + Export buttons, indeterminate progress bar, status label. Right panel: `QLabel` preview showing the result as a grayscale 8-bit `QPixmap` scaled to fit; `resizeEvent` is overridden to re-call `update_preview()` so the pixmap rescales when the window is resized. Run executes `normal_to_displacement()` via `QtConcurrent::run` + `QFutureWatcher`; `on_result_ready` assigns `result_` first, then calls `set_running(false)` (which enables Export via `!result_.height.empty()`), so Export state is always derived from the current result. Export saves a 16-bit PNG using `QImage::Format_Grayscale16` and `reinterpret_cast<quint16*>(img.scanLine(y))`.

## Known quirks

- **CGAL version**: this repo targets CGAL 6.x (installed as `cgal` on Arch). The API differs from CGAL 5.x in several ways: `merge_duplicate_points_in_polygon_soup` (not `merge_duplicated_vertices_in_polygon_soup`), `non_manifold_vertices` outputs `halfedge_descriptor` not `vertex_descriptor`.
- **lib3mf GCC 16 patch**: `cmake/patch_lib3mf_gcc16.cmake` is applied via `PATCH_COMMAND` in `FetchContent_Declare` and handles three GCC 16 breakages: (1) adds `#include <algorithm>` to six C++ source files that relied on transitive inclusion; (2) adds `#include <cstdint>` to `NMR_ModelTriangleSet.h` which uses `uint32_t` without including it; (3) injects `#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"` into two Windows-only libzip C sources that assign Win32 typed function pointers into `void*` struct fields. The pragma approach is necessary because `target_compile_options()` applied after `FetchContent_MakeAvailable()` does not reliably reach files inside the fetched dependency's own build system.
- **Windows portability — `M_PI`**: `M_PI` is a POSIX extension, not available in strict C++20 mode on Windows/MinGW. All uses in our code have been replaced with `std::numbers::pi` from `<numbers>` (C++20 standard library).
- **TBB parallel smoothing**: `MODELREPAIR_ENABLE_TBB=ON` (default) enables multi-core smoothing. When TBB is found, `TBB::tbb` is linked to `modelrepair` and `MODELREPAIR_HAVE_TBB` is defined, activating `std::execution::par_unseq` in the cotangent Laplacian vertex loop (`src/repairs/Smooth.cpp`). Without TBB the code compiles and runs sequentially — no functionality is lost. CGAL 6.x's `isotropic_remeshing` has no parallel API; only the smoothing step benefits from this.
- **Vulkan GPU smoothing**: `MODELREPAIR_ENABLE_VULKAN=ON` (default) enables GPU-accelerated smoothing. Requires `vulkan-headers`, `vulkan-icd-loader`, and `glslc` (from `shaderc`). When Vulkan + glslc are both found, `MODELREPAIR_HAVE_VULKAN` is defined, `VulkanSmoothing.cpp` is compiled, and the "Use GPU (Vulkan)" checkbox is enabled at runtime if a Vulkan device is detected. The GLSL shader `src/shaders/smooth_laplacian.comp` is compiled to SPIR-V at build time via `glslc`, then embedded as a `uint32_t[]` header (`smooth_laplacian_spv.h`) via `cmake/EmbedSpv.cmake`. Key approximation: cotangent weights and crease falloffs are precomputed once from the initial mesh (not updated per-iteration) and uploaded as a CSR sparse matrix — this avoids all CPU↔GPU round-trips between iterations. The "Use GPU (Vulkan)" checkbox is **hidden** (not just disabled) when `smooth_vulkan_available()` returns false, and is only enabled when smooth is checked AND Vulkan is available (`smooth_vulkan_available()` returns true; result is cached).
- **CGAL `isotropic_remeshing` is sequential**: CGAL 6.1.1's implementation (`remesh_impl.h`) has no `Parallel_tag` hook, no TBB usage. All phases (splits, collapses, flips, relaxation) run on a single thread.
- **Catch2 discovery**: `DISCOVERY_MODE PRE_TEST` is required because ASan makes the binary fail to start at CMake build-time (when Catch2 normally runs `--list-tests`).
- **EGL / Wayland CoreProfile**: On Wayland with the NVIDIA proprietary driver (EGL backend), requesting an OpenGL CoreProfile or MSAA samples in `QSurfaceFormat` produces `EGL_BAD_ATTRIBUTE` (error 3009) and the `QOpenGLWidget` fails to create a context. `main.cpp` therefore requests only a 24-bit depth buffer — no profile, no MSAA. The OpenGL 3.3 GLSL shaders still work because the default EGL context provides 3.3+ compatibility.
- **CLI `--decimate-backend` is case-sensitive**: Valid values are `cgal`, `meshoptimizer`, `openmesh` (all lowercase). Any other string emits a `Warning:` to stderr and falls back to cgal — it is not treated as an error, so check the warning when the backend matters.
- **CLI `--analyze-shells` in diagnose mode reports pre-repair topology**: `RepairPipeline::run()` with `diagnose_only=true` deep-copies the mesh and leaves the caller's copy unchanged. Shell analysis therefore runs on the original, unrepaired mesh and the output is labelled "Shell analysis (pre-repair, original mesh)".
- **CLI `--export-shells` ordering with `--keep-largest-shell`**: When both flags are given, shells are exported first (from the full repaired mesh), then `keep_shells(mesh, 1)` trims the mesh that gets written to OUTPUT. This means the shell files always reflect the full component count, not just the kept shell.

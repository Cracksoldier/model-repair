# CLAUDE.md

**GitHub:** https://github.com/Cracksoldier/model-repair

## Build

```bash
# System deps (Arch/CachyOS)
sudo pacman -S cgal eigen3 boost gmp mpfr qt6-base onetbb vulkan-headers vulkan-icd-loader shaderc

# Configure — downloads lib3mf, CLI11, spdlog, Catch2, tinygltf, meshoptimizer, OpenMesh via FetchContent
cmake --preset debug      # Debug + AddressSanitizer
cmake --preset release
cmake --preset release-nogui  # no Qt

cmake --build build/debug -j$(nproc)
```

Targets: `libmodelrepair.so`, `model-repair` (CLI), `model-repair-gui` (Qt).

## Tests

```bash
# Debug (ASan requires LD_PRELOAD)
LD_PRELOAD=$(gcc -print-file-name=libasan.so) ctest --preset debug --output-on-failure
# Single test by name or tag
LD_PRELOAD=$(gcc -print-file-name=libasan.so) \
  build/debug/tests/model-repair-tests "Fill holes closes an open surface"
LD_PRELOAD=$(gcc -print-file-name=libasan.so) build/debug/tests/model-repair-tests "[pipeline]"
# Release — no LD_PRELOAD needed
ctest --preset release --output-on-failure
```

`Test_CPP_Bindings` (lib3mf's own suite) is excluded by presets.

## Run

```bash
# Basic repair / diagnose
LD_LIBRARY_PATH=build/debug/src build/debug/cli/model-repair INPUT.stl OUTPUT.stl --verbose
LD_LIBRARY_PATH=build/debug/src build/debug/cli/model-repair broken.stl --diagnose --verbose

# Post-repair ops (can combine freely)
LD_LIBRARY_PATH=build/debug/src build/debug/cli/model-repair in.stl out.stl \
  --smooth 5 --smooth-vulkan --decimate 0.5 \
  --decimate-backend meshoptimizer --decimate-target-error 0.01 \
  --remove-internal-geometry

# Shell operations
LD_LIBRARY_PATH=build/debug/src build/debug/cli/model-repair multi.stl out.stl \
  --analyze-shells --keep-largest-shell --export-shells ./shells/

# GUI
build/debug/gui/model-repair-gui
LSAN_OPTIONS=suppressions=asan_suppressions.txt build/debug/gui/model-repair-gui
```

`--decimate-backend` values are case-sensitive: `cgal`, `meshoptimizer`, `openmesh`. See `--help` for all flags.

## Docs

`docs/index.html` (landing) and `docs/documentation.html` (API reference). Self-contained HTML, no build step. Served via GitHub Pages from `docs/` on `main`.

## Release

Two manually triggered workflows: `appimage.yml` (Linux AppImage) and `windows.yml` (Windows ZIP).
Inputs: `version` (e.g. `v0.1.0`), `create_release` (bool), `next_version` (bumps `CMakeLists.txt`). Releases are created as **drafts**.

**Linux key constraints:**
- Builds in `archlinux:latest` — CGAL 6.x is Arch-only
- `APPIMAGE_EXTRACT_AND_RUN=1` — linuxdeploy bypasses FUSE (unavailable in Docker)
- `NO_STRIP=1` — linuxdeploy's `strip` predates `.relr.dyn` ELF sections in modern Arch packages
- Custom libs copied to `/usr/lib` (not `LD_LIBRARY_PATH`) — avoids rpath-patched Qt libs breaking `linuxdeploy-plugin-qt`

**Windows key constraints:**
- MSYS2 UCRT64 ships Eigen3 5.x — use `find_package(Eigen3 REQUIRED NO_MODULE)` with no version
- `qt6_standard_project_setup()` puts all EXEs in `build/` — bundle step uses `find` not hardcoded paths
- GCC 16 patch via `cmake/patch_lib3mf_gcc16.cmake` for libzip `-Wincompatible-pointer-types`

## Architecture

### `libmodelrepair` (`src/`, `include/modelrepair/`)

Public API: **`RepairOptions`** (struct of pipeline flags + `diagnose_only`, `smooth`, `decimate`, `remesh`, `remove_internal_geometry`), **`RepairPipeline`** (`run(Mesh&)`; diagnose mode deep-copies the mesh), **`RepairReport`** (per-step `StepReport`; `surface_area_before/after`; `volume_before/after` as `optional<double>`).

Free functions (headers in `include/modelrepair/`):
- `analyze_wall_thickness(const Mesh&)` → `vector<double>` per-face via AABB_tree ray casting
- `analyze_shells` / `keep_shells` / `split_shells` — connected-component ops via `PMP::connected_components`
- `remove_internal_geometry(Mesh&)` — centroid `Side_of_triangle_mesh` test; pipeline step 7, off by default
- `subdivide(Mesh&, iters, method)` — Loop or Catmull-Clark with UV propagation to new vertices
- `displace_from_normal_map(Mesh&, params)` — requires `"v:uv"` map; uses blue/Z channel as pseudo-height
- `normal_to_displacement(path, settings, on_iteration, use_vulkan=false)` — image-in/image-out Poisson height map; CPU path: IC-PCG (50–150 iters, 8 MP limit); GPU path (`use_vulkan=true`): Jacobi-CG on Vulkan compute (`VulkanPoissonSolver`), ~3000 iters batched 25/cmd-buf, faster on ≥512×512. Falls back to CPU if GPU init fails. `normal_to_displacement_vulkan_available()` probes and caches device availability.
- `smooth(Mesh&, iters, crease_angle, on_iteration)` — cotangent Laplacian, dihedral-angle feature preservation, volume restore via uniform scaling (closed meshes only)
- `remesh(Mesh&, edge_length_factor, iters)` — isotropic remeshing; edges ≤ `factor × mean` constrained to freeze fine geometry
- `decimate(Mesh&, params)` — CGAL (edge_collapse), MeshOptimizer (meshopt_simplify), or OpenMesh (QEM); each saves/restores `"v:color"`

`Mesh` wraps `CGAL::Surface_mesh<EPICK::Point_3>`. Key methods: `cgal()`, `surface_area()`, `volume()` (→ `optional<double>`), `has_uv()`.

### Pipeline steps (`src/repairs/`)

Hard dependency chain — each step is a precondition for the next:

1. **MergeDuplicateVertices** — `PMP::repair_polygon_soup` (rebuilds from soup; STL always triggers this)
2. **RemoveDegenerateTriangles** — `PMP::remove_degenerate_faces`
3. **FixNonManifold** — `PMP::duplicate_non_manifold_vertices` (outputs `halfedge_descriptor`, not `vertex_descriptor`)
4. **FixNormals** — `PMP::orient`; skips `is_outward_oriented` on open meshes (it asserts `is_closed`)
5. **FillHoles** — `PMP::triangulate_and_refine_hole` per boundary cycle
6. **RemoveSelfIntersections** — converts to EPECK, `PMP::experimental::remove_self_intersections`, converts back (slowest step)
7. **RemoveInternalGeometry** — disabled by default; returns early when mesh is not closed

### I/O (`src/io/`)

`MeshIO.{hpp,cpp}` — format-agnostic load/save (`.stl`, `.obj`, `.3mf`, `.glb/.gltf`, `.ply`).

Per-vertex color stored as `"v:color"` (`CGAL::IO::Color`). Steps that rebuild from polygon soup (step 1, MeshOptimizer/OpenMesh decimation) save and restore the map. All color readers snapshot point count *before* `orient_polygon_soup` to guard against wrong-vertex assignment.

Non-obvious per-format notes:
- `GlbIO.cpp`: `TINYGLTF_HEADER_ONLY=ON` required (default mode is non-PIC). Colors only attached when *all* primitives have `COLOR_0`.
- `ObjIO`: parses `vt`/`f v/vt/vn` into per-vertex `"v:uv"`; map only attached when all corners have UVs and vertex count matches pre-orient count.

### GUI (`gui/`)

- **`MainWindow`** — central hub; runs `RepairWorker` on a `QThread`. Shared `cancel_flag_` (`shared_ptr<atomic<bool>>`). Buttons: Repair, Diagnose, Cancel, Batch Repair, Wizard, Separate Shells. Tools toolbar opens `NormalToDisplacementWindow`. Progress bar spans pipeline (7 steps) + remesh/smooth/decimate iterations as one unified count. Elapsed time shown as `M:SS / M:SS` (total / current step).
- **`RepairWorker`** — captures mesh copy, runs pipeline then post-repair ops (remesh → smooth → decimate). Cancellation checked at: pipeline `ProgressCallback`, after `run()`, per-iteration callbacks. Emits `finished(report, before, after, err)` or `cancelled(partial_mesh, steps_done)`.
- **`BatchWindow`** — sequential multi-file repair on a background thread; output to `_repaired` suffix or custom dir; close blocked while running.
- **`PreviewWindow`** — side-by-side Before/After with `MeshViewWidget` (OpenGL 3.3 flat Phong). Display modes: Normal / Wall Thickness (AABB heatmap) / Overhang. Shared `CameraState` (arcball + pan + zoom) across both widgets.
- **`WizardWindow`** — 3-phase QDialog: Phase 1 (repair), Phase 2 (remesh/smooth/subdivide/normal-displacement), Phase 3 (decimate). Each phase has options → running → preview states. Retry reverts `current_mesh_` to `phase_start_mesh_`. Phase 2 cancellable per-iteration via `WizardCancelled` sentinel (not `std::exception`); Phases 1/3 run to completion.
- **`NormalToDisplacementWindow`** — standalone Poisson height-map tool (no mesh). Qt 6 `QtConcurrent::run` with `QPromise`; cancels via `promise.isCanceled()`. "Use GPU (Vulkan)" checkbox is hidden when `normal_to_displacement_vulkan_available()` is false (same pattern as smooth). Export writes 16-bit grayscale PNG.

## Known quirks

- **CGAL 6.x API**: `merge_duplicate_points_in_polygon_soup` (not `merge_duplicated_vertices_in_polygon_soup`); `non_manifold_vertices` outputs `halfedge_descriptor` not `vertex_descriptor`.
- **lib3mf GCC 16 patch**: `cmake/patch_lib3mf_gcc16.cmake` adds missing `#include`s and `#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"` for libzip Windows sources.
- **TBB parallel smoothing**: `MODELREPAIR_ENABLE_TBB=ON` activates `std::execution::par_unseq` in the cotangent Laplacian loop. CGAL's `isotropic_remeshing` is always sequential (no Parallel_tag in 6.1.1).
- **Vulkan GPU smoothing**: cotangent weights precomputed once and uploaded as CSR matrix — avoids CPU↔GPU round-trips per iteration. Checkbox is *hidden* (not disabled) when `smooth_vulkan_available()` returns false.
- **Vulkan GPU Poisson solver** (`VulkanPoissonSolver`): Jacobi-preconditioned CG using 6 compute shaders (spmv, reduce, axpy, precond, scalar, pupdate). CPU initialises x=0, r=b, z=b/4, p=z, rz₀=‖b‖²/4 and uploads before the first dispatch. 25 CG iterations per command buffer; convergence checked by reading scalar_buf[0]=dot(r,z)=‖r‖²/4 against `initial_rz×tol²` (tol=1e-4).
- **ASan + PCG**: debug+ASan builds run PCG at ~10–50× slower than release due to shadow-memory cache misses. Use release for production normal-map work.
- **Catch2 discovery**: `DISCOVERY_MODE PRE_TEST` required — ASan makes the binary fail at CMake build-time otherwise.
- **EGL/Wayland CoreProfile**: NVIDIA EGL gives `EGL_BAD_ATTRIBUTE` if CoreProfile or MSAA is requested. `main.cpp` requests only a 24-bit depth buffer.
- **`M_PI`**: replaced with `std::numbers::pi` everywhere (POSIX extension, unavailable on Windows/MinGW).
- **`--decimate-backend`**: case-sensitive (`cgal`/`meshoptimizer`/`openmesh`); bad value warns to stderr and falls back to cgal.
- **`--analyze-shells` in diagnose mode**: runs on pre-repair mesh; output labelled accordingly.
- **`--export-shells` + `--keep-largest-shell`**: shells exported first (full mesh), then largest kept.

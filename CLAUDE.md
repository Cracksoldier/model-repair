# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# System deps (Arch/CachyOS)
sudo pacman -S cgal eigen3 boost gmp mpfr qt6-base

# Configure — downloads lib3mf, CLI11, spdlog, Catch2, tinygltf via FetchContent
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

# CLI — smooth blocky mesh (5 iterations of mean curvature flow)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair blocky.stl smooth.stl --smooth 5

# CLI — smooth then decimate (smooth runs first, intermediate is post-smooth)
LD_LIBRARY_PATH=build/debug/src \
  build/debug/cli/model-repair blocky.stl out.stl --smooth 3 --decimate 0.5

# GUI
build/debug/gui/model-repair-gui
```

## Release

Releases are built as Linux AppImages via a manually triggered GitHub Actions workflow.

### Workflow: `.github/workflows/appimage.yml`

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

## Architecture

The codebase is split into a shared library, two frontends, and a test suite.

### `libmodelrepair` (`src/`, `include/modelrepair/`)

The public API is three types plus two free functions:
- **`RepairOptions`** — plain struct of booleans/values, one per pipeline step. Additional fields: `diagnose_only` (run detection without modifying the mesh), `smooth` / `smooth_iterations` / `smooth_crease_angle` (post-repair feature-preserving smoothing), `decimate` / `decimate_ratio` (post-repair polygon-count reduction).
- **`RepairPipeline`** — call `run(Mesh&)`, optionally set a `ProgressCallback` first. When `opts.diagnose_only` is true, `run()` deep-copies the mesh, runs all steps on the copy, and returns a report without touching the original.
- **`RepairReport`** — returned by `run()`, contains a `StepReport` per step plus final mesh stats. Fields added: `surface_area_before/after` (mm², always populated), `volume_before/after` (mm³, `std::optional` — nullopt when the mesh is not closed), `diagnose_only`.
- **`smooth(Mesh&, unsigned int iterations, double crease_angle = 45.0)`** (`include/modelrepair/Smooth.hpp`, `src/repairs/Smooth.cpp`) — free function; two-phase: (1) feature-preserving Laplacian smoothing (λ=0.5) — each interior vertex averages only neighbours reachable via edges whose adjacent face normals differ by less than `crease_angle` degrees; edges at sharp features act as barriers, preserving costume seams, hard edges, and head crests while still smoothing flat areas; a vertex with no smooth neighbours stays put; at `crease_angle=180` it degenerates to uniform Laplacian; causes mild volume shrinkage (~2–4 % per 3 iterations); then (2) one pass of `PMP::angle_and_area_smoothing` with Delaunay flips cleans up sliver triangles. Pure EPICK, no EPECK conversion. Returns `SmoothResult{duration_ms}`.
- **`decimate(Mesh&, double ratio)`** (`include/modelrepair/Decimate.hpp`, `src/repairs/Decimate.cpp`) — free function; reduces face count to `ratio` fraction using CGAL `Surface_mesh_simplification` (`Face_count_ratio_stop_predicate`). Returns `DecimateResult{faces_before, faces_after, duration_ms}`.

`Mesh` is a thin wrapper around `CGAL::Surface_mesh<EPICK::Point_3>`. All repair steps work directly on `mesh.cgal()`. New methods: `surface_area()` → `double` and `volume()` → `std::optional<double>` (wraps `PMP::area` / `PMP::volume`; volume returns nullopt when the mesh is not closed).

### Repair pipeline step ordering (`src/repairs/`)

Each step file implements one private method of `RepairPipeline`. The order is a hard dependency chain — each step is a precondition for the next:

1. **MergeDuplicateVertices** — converts to polygon soup, calls `PMP::repair_polygon_soup` (which merges coincident points and removes degenerate/duplicate polygons), re-orients, rebuilds `Surface_mesh`. STL files always arrive as soup with no vertex sharing, so this step always reports merges.
2. **RemoveDegenerateTriangles** — `PMP::remove_degenerate_faces`. Zero-area faces left after step 1.
3. **FixNonManifold** — `PMP::duplicate_non_manifold_vertices` (outputs `halfedge_descriptor`, not `vertex_descriptor`).
4. **FixNormals** — `PMP::orient`. Skips `is_outward_oriented` check when the mesh is not closed (that function asserts `is_closed`).
5. **FillHoles** — `PMP::triangulate_and_refine_hole` per boundary cycle. Collects cycles via a visited-set traversal of border halfedges.
6. **RemoveSelfIntersections** — the only step that uses EPECK. Converts to `Surface_mesh<EPECK::Point_3>` via `CGAL::copy_face_graph`, calls `PMP::experimental::remove_self_intersections`, converts back. Slowest step.

### I/O (`src/io/`, `include/modelrepair/io/`)

`MeshIO.{hpp,cpp}` is the format-agnostic entry point (`load`/`save`, format inferred from extension). Each format calls CGAL polygon-soup helpers (`orient_polygon_soup` → `polygon_soup_to_polygon_mesh`) after reading raw geometry. Supported extensions: `.stl`, `.obj`, `.3mf`, `.glb`, `.gltf`.

- `ThreeMFIO.cpp` wraps `lib3mf`; its headers live in `build/<preset>/_deps/lib3mf-src/Autogenerated/Bindings/Cpp/` and are added as a PRIVATE include in `src/CMakeLists.txt`.
- `GlbIO.cpp` wraps `tinygltf` (header-only, fetched via FetchContent). `TINYGLTF_HEADER_ONLY=ON` is required — the default compiled-library mode produces non-PIC code that cannot link into the shared `.so`. `TINYGLTF_IMPLEMENTATION` is defined in `GlbIO.cpp` only, with stb_image disabled (`TINYGLTF_NO_STB_IMAGE` / `TINYGLTF_NO_INCLUDE_STB_IMAGE`). Both `.glb` (binary) and `.gltf` (text) are loadable; save always writes binary GLB.

### GUI (`gui/`)

`RepairWorker` (QObject) runs on a `QThread`. It captures a copy of the loaded mesh before calling `RepairPipeline::run` (which mutates the mesh in-place), then emits a four-argument `finished` signal carrying the `RepairReport`, the before-mesh, the after-mesh, and an error string. Progress signals cross the thread boundary via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. After `pipeline.run()`, post-repair operations run in order: if `opts_.smooth` is true and not diagnose-only, `RepairWorker` calls `smooth()` and appends a `StepReport`; then if `opts_.decimate` is true, calls `decimate()`, appends a `StepReport`, and updates `report.triangles_after`, `surface_area_after`, and `volume_after`.

`MainWindow` wires everything together; `ReportView` (QTreeWidget) renders the `RepairReport`. The report's summary row shows vertex/triangle counts plus surface area and volume (when the mesh is closed). On success, `MainWindow` opens a `PreviewWindow` (non-modal, `WA_DeleteOnClose`). A **"Diagnose"** button runs the pipeline with `diagnose_only = true`; the result is shown in the report tree but `btn_save_` is disabled and no preview opens. A **"Smooth after repair"** checkbox + iterations spinbox (1–50, default 3) + crease-angle spinbox (0–180°, default 45°) controls post-repair feature-preserving smoothing. A **"Decimate after repair"** checkbox + ratio spinbox (0.01–1.00, default 0.50) controls post-repair decimation. Both smooth and decimate are disabled automatically in diagnose mode. A **"Batch Repair…"** button opens a `BatchWindow` (non-modal, `WA_DeleteOnClose`) pre-populated with the current repair options.

`BatchWindow` processes a list of files sequentially on a background `QThread` (one `RepairWorker` per file, reused). Files are added via drag-and-drop or "Add Files…". Output defaults to the same directory as the input with a `_repaired` filename suffix; an alternative output directory can be set with "Browse…". A table shows Filename / Status / Issues / Watertight / Time per file. "Preview Selected" opens a `PreviewWindow` for the selected completed job. The window cannot be closed while a repair is running (close event is ignored; click Cancel first).

`PreviewWindow` shows a side-by-side Before/After 3D comparison. It owns copies of both meshes as value members. Each `MeshViewWidget` (QOpenGLWidget) renders one mesh using OpenGL 3.3 with flat-shaded Phong lighting (GLSL 3.30, flat triangle VBO with interleaved `[pos, normal]`). Both widgets share a single `std::shared_ptr<CameraState>` (rotation as QQuaternion, zoom, pan, scene_radius). When any camera-mutating mouse event fires the emitting widget calls `emit camera_changed()`; the peer is connected to that signal and calls `update()` — no mutex needed because both live in the main thread. Mouse controls: left-drag = arcball rotation, right-drag = XY pan, scroll = zoom.

## Known quirks

- **CGAL version**: this repo targets CGAL 6.x (installed as `cgal` on Arch). The API differs from CGAL 5.x in several ways: `merge_duplicate_points_in_polygon_soup` (not `merge_duplicated_vertices_in_polygon_soup`), `non_manifold_vertices` outputs `halfedge_descriptor` not `vertex_descriptor`.
- **lib3mf GCC 16 patch**: `cmake/patch_lib3mf_gcc16.cmake` is applied via `PATCH_COMMAND` in `FetchContent_Declare` and adds `#include <algorithm>` to six lib3mf source files that GCC 16 requires it in explicitly.
- **Catch2 discovery**: `DISCOVERY_MODE PRE_TEST` is required because ASan makes the binary fail to start at CMake build-time (when Catch2 normally runs `--list-tests`).
- **EGL / Wayland CoreProfile**: On Wayland with the NVIDIA proprietary driver (EGL backend), requesting an OpenGL CoreProfile or MSAA samples in `QSurfaceFormat` produces `EGL_BAD_ATTRIBUTE` (error 3009) and the `QOpenGLWidget` fails to create a context. `main.cpp` therefore requests only a 24-bit depth buffer — no profile, no MSAA. The OpenGL 3.3 GLSL shaders still work because the default EGL context provides 3.3+ compatibility.

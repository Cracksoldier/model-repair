# model-repair

A native Linux 3D mesh repair tool — a drop-in replacement for the Windows-only "Fix Model" feature in Bambu Studio (which delegates to Microsoft Netfabb). Repairs the mesh defects that prevent successful FDM/resin printing: non-manifold geometry, holes, bad normals, degenerate triangles, and self-intersections.

Ships as two frontends over a shared C++ library:

- **`model-repair`** — command-line tool, scriptable and composable
- **`model-repair-gui`** — Qt 6 desktop application with drag-and-drop, a per-step repair report, and a side-by-side 3D Before/After preview

Supported formats: **STL** (binary + ASCII), **OBJ**, **3MF**, **PLY** (ASCII + binary LE/BE), **GLB/glTF**.

Per-vertex RGB color is preserved round-trip for PLY, OBJ (the `v x y z r g b` extension), and GLB/glTF (`COLOR_0` attribute). UV coordinates are read from OBJ files (`vt` lines) and stored as a per-vertex map — enabling Normal Map Displacement in the Wizard.

---

## What it fixes

| Problem | Description |
|---|---|
| Duplicate vertices | STL files store triangles as independent vertex triples with no sharing. Merging them is required before any topology analysis. |
| Degenerate triangles | Zero-area faces with collinear or coincident vertices. These corrupt normal computation and hole detection. |
| Non-manifold geometry | Edges shared by more than two faces, or vertex fans that are not disk-topology. Slicers cannot compute infill or walls on non-manifold meshes. |
| Inconsistent normals | Faces whose winding order points inward instead of outward. Causes inside-out or partially inverted prints. |
| Holes / open boundaries | Missing faces left by degenerate removal, non-manifold splitting, or upstream CAD errors. |
| Self-intersections | Overlapping triangles from boolean CSG operations or poorly exported models. |
| Internal geometry | Faces hidden inside a closed mesh (redundant shell fragments). Detected via centroid inside-test and removed automatically. |

---

## Dependencies

### System packages

Install with your package manager. Examples below use **pacman** (Arch / CachyOS / Manjaro):

```bash
sudo pacman -S cgal eigen3 boost gmp mpfr qt6-base
```

| Package | Min version | Purpose |
|---|---|---|
| `cgal` | 5.6 | Core mesh repair algorithms (Polygon Mesh Processing) |
| `eigen3` | 3.4 | Linear algebra, required by CGAL |
| `boost` | 1.74 | Required by CGAL |
| `gmp` | any | Exact arithmetic kernel (CGAL) |
| `mpfr` | any | Multi-precision floats (CGAL) |
| `qt6-base` | 6.4 | GUI (Widgets + Concurrent + OpenGL modules) |
| `cmake` | 3.25 | Build system |
| `git` | any | FetchContent dependency downloads |

**Ubuntu / Debian:**

```bash
sudo apt install libcgal-dev libeigen3-dev libboost-all-dev libgmp-dev \
                 libmpfr-dev qt6-base-dev cmake git
```

**Fedora / RHEL:**

```bash
sudo dnf install CGAL-devel eigen3-devel boost-devel gmp-devel mpfr-devel \
                 qt6-qtbase-devel cmake git
```

### Auto-fetched dependencies (no action needed)

These are downloaded automatically by CMake at configure time via `FetchContent`:

| Library | Version | License | Purpose |
|---|---|---|---|
| lib3mf | v2.4.1 | BSD-2-Clause | 3MF file format (reference implementation) |
| CLI11 | v2.4.2 | BSD-3-Clause | Command-line argument parsing |
| spdlog | v1.15.3 | MIT | Structured logging and progress output |
| tinygltf | v2.8.21 | MIT | GLB/glTF 2.0 mesh I/O (header-only) |
| meshoptimizer | v0.22 | MIT | Fast error-bounded mesh simplification backend |
| OpenMesh | commit `4e2e481` | BSD-3-Clause | QEM-based mesh simplification backend (static) |
| Catch2 | v3.8.1 | BSL-1.0 | Test framework (only if tests are enabled) |

An internet connection is required on first configure. Subsequent configures use the CMake build cache.

> **OpenMesh note:** OpenMesh is licensed BSD-3-Clause and statically linked into `libmodelrepair.so`. The decimation backends (MeshOptimizer and OpenMesh) can be individually disabled at configure time with `MODELREPAIR_ENABLE_MESHOPTIMIZER=OFF` / `MODELREPAIR_ENABLE_OPENMESH=OFF`.

> **GCC 16 note:** lib3mf v2.4.1 is missing `#include <algorithm>` in several source files, which GCC 16 now requires explicitly. The file `cmake/patch_lib3mf_gcc16.cmake` is applied automatically via `FetchContent PATCH_COMMAND` and fixes all affected files transparently.

---

## Building

### Quick start (debug build with AddressSanitizer)

```bash
# 1. Install system dependencies (see above)

# 2. Clone or enter the project directory
cd model-repair

# 3. Configure — downloads lib3mf, CLI11, spdlog, tinygltf, meshoptimizer, OpenMesh, Catch2 automatically
cmake --preset debug

# 4. Build everything (library + CLI + GUI + tests)
cmake --build build/debug -j$(nproc)
```

### Release build

```bash
cmake --preset release
cmake --build build/release -j$(nproc)
```

### Build without GUI (CLI only)

Useful on headless servers or if Qt 6 is not available:

```bash
cmake --preset release-nogui
cmake --build build/release-nogui -j$(nproc)
```

### CMake options

Pass these with `-D` if you configure manually instead of using a preset:

| Option | Default | Description |
|---|---|---|
| `MODELREPAIR_BUILD_CLI` | `ON` | Build the `model-repair` CLI tool |
| `MODELREPAIR_BUILD_GUI` | `ON` | Build the `model-repair-gui` Qt application |
| `MODELREPAIR_BUILD_TESTS` | `ON` | Build the Catch2 test suite |
| `MODELREPAIR_ENABLE_ASAN` | `OFF` | Enable AddressSanitizer + UBSan (debug builds) |
| `MODELREPAIR_ENABLE_MESHOPTIMIZER` | `ON` | Fetch and compile the meshoptimizer decimation backend |
| `MODELREPAIR_ENABLE_OPENMESH` | `ON` | Fetch and compile the OpenMesh (QEM) decimation backend |
| `MODELREPAIR_ENABLE_TBB` | `ON` | Use Intel TBB for parallel CPU smoothing |
| `MODELREPAIR_ENABLE_VULKAN` | `ON` | Build Vulkan GPU-accelerated smoothing |

Example — library only, no GUI, no tests:

```bash
cmake -B build/minimal -DMODELREPAIR_BUILD_GUI=OFF -DMODELREPAIR_BUILD_TESTS=OFF
cmake --build build/minimal -j$(nproc)
```

---

## Running

### GUI

```bash
./build/debug/gui/model-repair-gui
```

1. Drop an STL / OBJ / 3MF / PLY / GLB file onto the drop zone, or click **Open file…**
2. Adjust repair options if needed (all enabled by default)
3. Click **Repair** — progress is shown per step in real time
4. A **Before / After 3D preview window** opens automatically when repair completes — rotate with left-drag, pan with right-drag, zoom with scroll wheel; both views are camera-synced
5. Review the per-step report in the main window
6. Click **Save As…** to export the repaired mesh
7. Or click **Wizard…** for a guided three-phase workflow: Phase 1 repairs the mesh, Phase 2 optionally remeshes/smooths/subdivides it and can bake a normal map into geometry [highly experimental], Phase 3 optionally decimates it — each phase shows a before/after 3D preview before you commit
8. Use the **Tools** dropdown in the toolbar to open standalone tools — currently: **Normal Map → Displacement** (converts a tangent-space normal map PNG to a 16-bit grayscale height map via Poisson reconstruction; no mesh required; limit: ≤8 MP; shows a live progress bar and elapsed/ETA label; Cancel stops the solver within one iteration)

### CLI

```bash
model-repair INPUT OUTPUT [OPTIONS]
```

**Basic usage:**

```bash
# Repair with all steps enabled, binary STL output
./build/debug/cli/model-repair broken.stl repaired.stl

# Verbose — show per-step progress and timing
./build/debug/cli/model-repair broken.stl repaired.stl --verbose

# Write a JSON repair report alongside the output
./build/debug/cli/model-repair broken.stl repaired.stl --report report.json

# Convert STL to 3MF while repairing
./build/debug/cli/model-repair broken.stl repaired.3mf

# Skip the slow self-intersection pass
./build/debug/cli/model-repair broken.stl repaired.stl --no-remove-self-intersections

# Only fill holes with fewer than 20 boundary edges
./build/debug/cli/model-repair broken.stl repaired.stl --max-hole-edges 20

# Enable step 7: remove geometry hidden inside the mesh
./build/debug/cli/model-repair broken.stl repaired.stl --remove-internal-geometry

# Smooth a blocky voxel mesh, then decimate to 50 % of faces
./build/debug/cli/model-repair blocky.stl out.stl --smooth 5 --decimate 0.5

# GPU-accelerated smoothing (falls back to CPU if Vulkan unavailable)
./build/debug/cli/model-repair blocky.stl out.stl --smooth 10 --smooth-vulkan

# Decimate with the fast MeshOptimizer backend
./build/debug/cli/model-repair model.stl out.stl --decimate 0.5 --decimate-backend meshoptimizer

# Decimate with OpenMesh QEM backend, limiting normal deviation to 20°
./build/debug/cli/model-repair model.stl out.stl --decimate 0.5 \
  --decimate-backend openmesh --decimate-normal-dev 20

# Diagnose — analyse connected shells without modifying the mesh
./build/debug/cli/model-repair multi.stl --diagnose --analyze-shells

# Keep only the largest connected component after repair
./build/debug/cli/model-repair multi.stl cleaned.stl --keep-largest-shell

# Export every shell as a separate file into a directory (OUTPUT is optional)
./build/debug/cli/model-repair multi.stl --export-shells ./shells/
```

**All options:**

```
model-repair INPUT [OUTPUT] [OPTIONS]

Positional:
  INPUT                         Input mesh (.stl, .obj, .3mf, .ply, .glb, .gltf)
  OUTPUT                        Output mesh (format inferred from extension)
                                Optional when --diagnose or --export-shells is used

Repair steps (all enabled by default):
  --no-merge-vertices           Skip duplicate vertex merging
  --merge-tolerance FLOAT       Merge distance tolerance [1e-06]
  --no-remove-degenerate        Skip degenerate triangle removal
  --no-fix-non-manifold         Skip non-manifold geometry repair
  --no-fix-normals              Skip face normal orientation fix
  --no-fill-holes               Skip hole filling
  --max-hole-edges INT          Skip holes with > N boundary edges (0=all) [0]
  --flat-fill                   Flat fan fill instead of smooth refinement
  --no-remove-self-intersections  Skip self-intersection removal (slow)
  --remove-internal-geometry    Enable step 7: remove faces inside the closed mesh [off]

Post-repair operations (run after all repair steps, in this order):
  --remesh FLOAT                Isotropic remesh before smoothing: edge length factor (0.1–2.0)
  --remesh-iterations INT       Remesh iteration count [3] (1–10)
  --smooth INT                  Smooth after repair: number of Laplacian iterations (1–50)
  --smooth-crease-angle FLOAT   Feature-preservation crease angle in degrees [45] (0–180)
  --smooth-vulkan               Use Vulkan GPU for smoothing (falls back to CPU if unavailable)
  --decimate FLOAT              Reduce face count to this fraction of original (0.01–1.0)
  --decimate-backend TEXT       Decimation backend: cgal (default), meshoptimizer, openmesh
  --decimate-target-error FLOAT MeshOptimizer: relative error budget [0.01] (0.0001–1.0)
  --decimate-normal-dev FLOAT   OpenMesh: normal deviation limit in degrees [15] (1–90)

Shell operations (applied after all post-repair ops):
  --analyze-shells              Print connected-component (shell) analysis
  --keep-largest-shell          Discard all but the largest connected component
  --export-shells DIR           Split mesh into one file per shell, saved to DIR

Diagnose:
  --diagnose                    Report issues without modifying the mesh. OUTPUT is optional.

Output:
  --ascii-stl                   Write ASCII STL instead of binary
  --report PATH                 Write repair report to JSON file
  --report-format TEXT          Report format on stdout: text or json [text]

Logging:
  -v, --verbose                 Print per-step progress and timing
  -q, --quiet                   Suppress all output except errors
  --version                     Print version and exit
  -h, --help                    Print help and exit

Batch subcommand:
  model-repair batch FILES... [--output-dir DIR] [--batch-report PATH]
```

**Exit codes:**

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Input/output path error or batch failures |
| 3 | Repair step failed |
| 4 | Output write error |
| 5 | Decimation failed |
| 6 | Smoothing failed |
| 7 | Remeshing failed |
| 8 | Shell export failed (directory missing or save error) |

---

## Running the tests

```bash
# Run all tests with verbose output (debug build uses ASan, so preload it)
LD_PRELOAD=$(gcc -print-file-name=libasan.so) ctest --preset debug --output-on-failure

# Re-run only failed tests
LD_PRELOAD=$(gcc -print-file-name=libasan.so) ctest --preset debug --rerun-failed --output-on-failure

# Release build — no ASan, no preload needed
ctest --preset release --output-on-failure
```

Test meshes are generated automatically by `tests/generate_test_meshes.py` during the build. Each mesh has a single known defect:

| Mesh | Defect injected |
|---|---|
| `valid_cube.stl` | None — baseline, must survive repair unchanged |
| `inverted_normals.stl` | All face windings reversed |
| `open_surface.stl` | Two faces removed (open boundary) |
| `duplicate_vertices.stl` | No vertex sharing (raw STL triangle soup) |
| `degenerate_triangles.stl` | 5 zero-area faces injected |
| `non_manifold_edge.stl` | Two cubes sharing a face (edge shared by 3 triangles) |
| `self_intersecting.stl` | Two cubes overlapping by 50% |

---

## Repair pipeline

Steps are applied in this order because each step is a precondition for the next:

```
Input mesh (polygon soup from STL/OBJ/3MF)
    │
    ▼
1. Merge duplicate vertices
   CGAL: merge_duplicated_vertices_in_polygon_soup → polygon_soup_to_polygon_mesh
   Converts raw triangle soup into a proper half-edge mesh with shared vertices.

    ▼
2. Remove degenerate triangles
   CGAL: remove_degenerate_faces
   Eliminates zero-area faces before any topology analysis.

    ▼
3. Fix non-manifold geometry
   CGAL: duplicate_non_manifold_vertices
   Splits vertices where the face fan is not disk-topology, ensuring every
   edge is shared by exactly two faces.

    ▼
4. Fix face normals
   CGAL: orient (per connected component)
   Flips components that are wound inward so all normals point outward.

    ▼
5. Fill holes
   CGAL: triangulate_and_refine_hole (smooth) or triangulate_hole (flat)
   Patches open boundary cycles left by steps 2–3.

    ▼
6. Remove self-intersections  [slow — uses EPECK exact kernel]
   CGAL: experimental::remove_self_intersections
   Heals overlapping triangles. Converts to exact-construction kernel
   internally for robustness, then converts back.

    ▼
7. Remove internal geometry  [optional — disabled by default]
   CGAL: Side_of_triangle_mesh
   Removes faces whose centroid lies inside the closed mesh.
   Skipped when the mesh is not closed or the option is off.

    ▼
Repaired mesh (manifold, closed, outward-oriented)
```

---

## Architecture

```
libmodelrepair.so              (shared library — LGPL-safe via dynamic linking)
├── Mesh                       CGAL Surface_mesh wrapper
├── RepairPipeline             Ordered repair orchestration (7 steps)
├── RepairOptions              Plain struct — all tunable parameters
├── RepairReport               Per-step statistics (issues found/fixed, timing)
├── WallThickness              Per-face AABB ray-cast thickness analysis
├── ShellSeparation            Connected-component detection, keep/split shells
├── RemoveInternalGeometry     Centroid inside-test, removes hidden faces
├── Subdivide                  Loop / Catmull-Clark mesh subdivision (UV-propagating)
├── NormalMapDisplace          Bake tangent-space normal map detail into geometry
├── NormalToDisplacement       Poisson height-map reconstruction from normal map (image→image)
└── io/                        STL / OBJ / 3MF / PLY / GLB / GLTF readers and writers

model-repair                   CLI frontend (links libmodelrepair)
model-repair-gui               Qt 6 frontend (links libmodelrepair)
├── MainWindow                 Main UI — options, progress, report, shell separation, Tools menu
├── BatchWindow                Multi-file batch repair (QDialog)
├── WizardWindow               Guided three-phase repair workflow (QDialog)
├── WizardWorker               Per-phase background worker (QObject/QThread)
├── NormalToDisplacementWindow Normal map → displacement map converter (Tools dropdown)
├── PreviewWindow              Side-by-side Before/After 3D window with shading modes
└── MeshViewWidget             QOpenGLWidget — Phong + heatmap shading, arcball camera
```

The library is built as a shared object so CGAL's LGPL terms are satisfied by dynamic linking even if the frontends are distributed under a different license.

---

## Project structure

```
model-repair/
├── CMakeLists.txt
├── CMakePresets.json          debug / release / release-nogui
├── cmake/
│   ├── FetchDependencies.cmake
│   └── CompilerWarnings.cmake
├── include/modelrepair/       Public headers (install interface)
│   ├── Mesh.hpp
│   ├── RepairOptions.hpp
│   ├── RepairReport.hpp
│   ├── RepairPipeline.hpp
│   ├── WallThickness.hpp
│   ├── ShellSeparation.hpp
│   ├── RemoveInternalGeometry.hpp
│   ├── Subdivide.hpp
│   ├── NormalMapDisplace.hpp
│   ├── Remesh.hpp
│   ├── Smooth.hpp
│   ├── Decimate.hpp
│   └── io/  StlIO.hpp  ObjIO.hpp  ThreeMFIO.hpp  GlbIO.hpp  PlyIO.hpp  MeshIO.hpp
├── src/                       Library implementation
│   ├── repairs/               One .cpp per repair step + new analysis/processing
│   ├── analysis/              WallThickness.cpp
│   └── io/                    One .cpp per format
├── cli/main.cpp               CLI tool
├── gui/                       Qt 6 application
│   ├── MainWindow.cpp/.hpp
│   ├── RepairWorker.cpp/.hpp  QThread wrapper for background repair
│   ├── ReportView.cpp/.hpp    QTreeWidget repair report display
│   ├── PreviewWindow.cpp/.hpp Side-by-side Before/After 3D window + shading modes
│   ├── BatchWindow.cpp/.hpp   Multi-file batch repair dialog
│   ├── WizardWindow.cpp/.hpp  Guided three-phase repair wizard (QDialog)
│   ├── WizardWorker.cpp/.hpp  Per-phase background worker for the wizard
│   └── MeshViewWidget.cpp/.hpp QOpenGLWidget — heatmap shader, arcball camera
└── tests/
    ├── generate_test_meshes.py
    ├── test_mesh_io.cpp
    ├── test_repairs.cpp       One test case per repair step
    └── test_pipeline.cpp      End-to-end integration tests
```

---

## Integrating with Bambu Studio on Linux

The CLI tool can be wired into Bambu Studio's "Fix Model" context menu via a shell script. Bambu Studio on Linux does not call an external repair service, but you can add a custom script menu entry or pre-process models before import:

```bash
#!/bin/bash
# wrap-repair.sh — repair a model and open it in Bambu Studio
INPUT="$1"
REPAIRED="${INPUT%.stl}_repaired.stl"
model-repair "$INPUT" "$REPAIRED" --verbose && bambu-studio "$REPAIRED"
```

---

## License

- **model-repair application code**: MIT
- **CGAL** (Polygon Mesh Processing): LGPL 3.0 — linked dynamically
- **lib3mf**: BSD 2-Clause
- **CLI11**: BSD 3-Clause
- **spdlog**: MIT
- **tinygltf**: MIT
- **meshoptimizer**: MIT
- **OpenMesh**: BSD 3-Clause — statically linked into `libmodelrepair.so`
- **Catch2**: BSL-1.0
- **Qt 6**: LGPL 3.0 — linked dynamically

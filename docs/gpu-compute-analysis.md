# GPU Compute Analysis: Remesh / Smooth / Decimate

Design analysis for whether and where GPU acceleration makes sense in the
model-repair pipeline. **Nothing has been implemented yet** — this is a
decision record.

---

## TL;DR

| Operation | Right tool | Rationale |
|---|---|---|
| Isotropic remeshing | CPU sequential (CGAL) | CGAL 6.x `isotropic_remeshing` is fully sequential — no `Parallel_tag` hook exists |
| Smoothing | `std::execution::par_unseq` + TBB | Fixed topology, embarrassingly parallel per-vertex, many iterations — **implemented** |
| Decimation | CPU sequential | Inherently sequential greedy algorithm |
| Repair pipeline (6 steps) | CPU sequential | Topology analysis, not compute-bound |

---

## Why Not GPU for Everything

CGAL's `Surface_mesh` is a pointer-based half-edge structure. It is not
GPU-friendly. Any GPU approach requires marshaling the mesh into flat arrays,
doing compute, then writing results back. Whether that round-trip pays off
depends on how much the operation is dominated by arithmetic vs. topology
traversal.

GPU compute makes sense when:
- Topology does **not** change during the operation
- The operation is embarrassingly parallel per vertex/face
- There are enough iterations (or vertices) to amortize setup overhead

---

## Operation-by-Operation

### Decimation — CPU sequential, no change

`Surface_mesh_simplification` is a greedy edge-collapse algorithm driven by a
priority queue. Each collapse invalidates neighboring edges; the next best
collapse depends on the result of the previous one. This is fundamentally
sequential. Even state-of-the-art parallel decimation research achieves only
modest gains at high implementation cost. Not worth pursuing.

### Isotropic Remeshing — CPU sequential, no parallelism available

**Correction:** CGAL 6.1.1's `isotropic_remeshing` is **entirely sequential**.
After inspecting `/usr/include/CGAL/Polygon_mesh_processing/remesh.h` and
`internal/Isotropic_remeshing/remesh_impl.h` in CGAL 6.1.1: there is no
`Parallel_tag` named parameter, no TBB usage, no `CGAL_LINKED_WITH_TBB` guard.
All phases — edge splits, collapses, flips, tangential relaxation — run on a
single thread.

The prep loops in our own `Remesh.cpp` (edge-length collection, constraint
marking) are O(E) and could use `par_unseq`, but those loops are negligible
compared to the CGAL call itself, so the gain would be below noise.

### Smoothing — `std::execution::par_unseq` + TBB (**implemented**)

During the smoothing loop, topology **never changes** — only vertex positions
move. Each iteration is a weighted graph Laplacian applied to vertex positions:

```
new_pos[v] = pos[v] + λ * Σ(w_ij * (pos[j] - pos[v])) / Σ(w_ij)
```

where `w_ij = (cot(α) + cot(β)) * crease_falloff(dihedral_angle)`.

This is a sparse matrix-vector product — a textbook GPU operation. The key
insight is that the cotangent weights and crease falloffs depend only on mesh
connectivity and the current face normals, which are fixed for the duration of
a `smooth()` call. They only need to be computed once and uploaded once, even
across 50 iterations.

**What's implemented (CPU parallel, C++20 stdlib):**

The vertex-update loop in `src/repairs/Smooth.cpp` is embarrassingly parallel:
each vertex reads only current neighbour positions (read-only during the loop)
and writes to its own exclusive slot in the `buf` output array. No data races.

`std::for_each(std::execution::par_unseq, ...)` is used for both the
computation pass and the apply-back pass. On GCC/libstdc++, `par_unseq`
dispatches to TBB threads when `libtbb` is linked — which is enabled by the
optional CMake dep `MODELREPAIR_ENABLE_TBB=ON` (default). On platforms without
TBB the code falls back to the sequential `std::for_each` path (`#else` branch
guarded by `MODELREPAIR_HAVE_TBB`).

**If GPU compute is pursued later:**

Vulkan compute remains the long-term ceiling. Pre-compute Laplacian weights +
crease falloffs once per `smooth()` call (connectivity is fixed), store in CSR
SSBOs, dispatch one compute pass per iteration. See the Vulkan section below
for cross-platform viability details.

### Repair Pipeline (6 steps) — CPU sequential, no change

The six repair steps are topology analysis and correction: manifold detection,
hole filling, self-intersection resolution. They are not compute-bound. The
bottleneck is algorithmic complexity. Self-intersections use the EPECK kernel
(exact arithmetic in software) — no GPU equivalent exists. No GPU path makes
sense here.

---

## Vulkan Compute — Cross-Platform Viability

Vulkan is the right GPU compute API for this project's target platforms.

**Driver coverage:**
- NVIDIA: excellent on Linux and Windows
- AMD: excellent (RADV on Linux is production quality; Windows AMD drivers are good)
- Intel: good (ANV on Linux; Intel Windows driver)
- Integrated graphics: generally supported

**Windows build:** MSYS2 UCRT64 packages `vulkan-loader` and `vulkan-headers`,
so it slots directly into the existing GitHub Actions workflow.

**Verbosity concern:** Raw Vulkan requires hundreds of lines of boilerplate per
compute pass. The library **Kompute** (Vulkan-native GPGPU, header-only C++)
reduces this significantly — roughly: write a GLSL compute shader, plus ~50
lines of host code to set up buffers and dispatch. Kompute is MIT-licensed.

**Shader compilation:** GLSL compute shaders compile to SPIR-V at build time
via `glslc` (part of `shaderc`, available as an MSYS2 package and on Arch).
SPIR-V blobs embed as `uint32_t[]` arrays — no runtime shader compiler needed
in the distributed binary.

---

## Implementation Status

1. **~~CGAL `Parallel_tag` for remeshing~~** — not possible; CGAL 6.x
   `isotropic_remeshing` is entirely sequential with no parallel API.
2. **`std::execution::par_unseq` for smoothing** — **done** (`src/repairs/Smooth.cpp`).
   TBB linked as optional CMake dep (`MODELREPAIR_ENABLE_TBB`, default ON).
   Guarded by `#ifdef MODELREPAIR_HAVE_TBB` — falls back to sequential if TBB
   not found at build time.
3. **Vulkan compute for smoothing** — future option if profiling shows CPU
   parallel still bottlenecks on very large meshes (≥ 500K vertices). See
   Vulkan section above for cross-platform feasibility notes.

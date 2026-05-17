#!/usr/bin/env python3
"""Generate test STL meshes with known defects for the model-repair test suite."""

import struct
import sys
import os
import math

def write_binary_stl(path, triangles):
    """Write a binary STL file. triangles is a list of (normal, v0, v1, v2) tuples."""
    with open(path, 'wb') as f:
        header = b'model-repair test mesh' + b'\x00' * (80 - 22)
        f.write(header)
        f.write(struct.pack('<I', len(triangles)))
        for normal, v0, v1, v2 in triangles:
            f.write(struct.pack('<3f', *normal))
            f.write(struct.pack('<3f', *v0))
            f.write(struct.pack('<3f', *v1))
            f.write(struct.pack('<3f', *v2))
            f.write(struct.pack('<H', 0))

def cube_triangles():
    """12 triangles forming a unit cube [0,1]^3 with correct outward normals."""
    tris = []
    faces = [
        # -Z face (normal 0,0,-1)
        ((0,0,-1), (0,0,0), (1,0,0), (1,1,0)),
        ((0,0,-1), (0,0,0), (1,1,0), (0,1,0)),
        # +Z face (normal 0,0,+1)
        ((0,0,+1), (0,0,1), (0,1,1), (1,1,1)),
        ((0,0,+1), (0,0,1), (1,1,1), (1,0,1)),
        # -Y face (normal 0,-1,0)
        ((0,-1,0), (0,0,0), (0,0,1), (1,0,1)),
        ((0,-1,0), (0,0,0), (1,0,1), (1,0,0)),
        # +Y face (normal 0,+1,0)
        ((0,+1,0), (0,1,0), (1,1,0), (1,1,1)),
        ((0,+1,0), (0,1,0), (1,1,1), (0,1,1)),
        # -X face (normal -1,0,0)
        ((-1,0,0), (0,0,0), (0,1,0), (0,1,1)),
        ((-1,0,0), (0,0,0), (0,1,1), (0,0,1)),
        # +X face (normal +1,0,0)
        ((+1,0,0), (1,0,0), (1,0,1), (1,1,1)),
        ((+1,0,0), (1,0,0), (1,1,1), (1,1,0)),
    ]
    return faces

def valid_cube(out_dir):
    write_binary_stl(os.path.join(out_dir, 'valid_cube.stl'), cube_triangles())

def inverted_normals(out_dir):
    """All face normals flipped (vertices reversed)."""
    tris = [(n, v2, v1, v0) for (n, v0, v1, v2) in cube_triangles()]
    write_binary_stl(os.path.join(out_dir, 'inverted_normals.stl'), tris)

def open_surface(out_dir):
    """Cube with two faces removed (open boundaries)."""
    tris = cube_triangles()[4:]  # remove first 4 triangles (two faces)
    write_binary_stl(os.path.join(out_dir, 'open_surface.stl'), tris)

def duplicate_vertices(out_dir):
    """Cube where each triangle has its own private copy of its vertices (no sharing).
    This is how raw STL files look — the merge step collapses them."""
    write_binary_stl(os.path.join(out_dir, 'duplicate_vertices.stl'), cube_triangles())
    # The triangles already have private vertices since write_binary_stl
    # writes raw tuples; this is exactly the STL polygon-soup format.

def degenerate_triangles(out_dir):
    """Valid cube plus 5 zero-area degenerate triangles."""
    tris = list(cube_triangles())
    p = (0.5, 0.5, 0.5)
    for _ in range(5):
        tris.append(((0, 0, 1), p, p, p))  # all three vertices identical
    write_binary_stl(os.path.join(out_dir, 'degenerate_triangles.stl'), tris)

def non_manifold_edge(out_dir):
    """Two cubes sharing a face edge — that edge will be shared by 3 triangles."""
    tris = list(cube_triangles())
    # Second cube offset by (1,0,0) — shares the +X face of the first cube
    offset = (1, 0, 0)
    def shift(v):
        return (v[0] + offset[0], v[1] + offset[1], v[2] + offset[2])
    for (n, v0, v1, v2) in cube_triangles():
        tris.append((n, shift(v0), shift(v1), shift(v2)))
    write_binary_stl(os.path.join(out_dir, 'non_manifold_edge.stl'), tris)

def self_intersecting(out_dir):
    """Two unit cubes overlapping (one offset by 0.5 in X) — creates self-intersections."""
    tris = list(cube_triangles())
    def shift(v):
        return (v[0] + 0.5, v[1], v[2])
    for (n, v0, v1, v2) in cube_triangles():
        tris.append((n, shift(v0), shift(v1), shift(v2)))
    write_binary_stl(os.path.join(out_dir, 'self_intersecting.stl'), tris)

if __name__ == '__main__':
    out_dir = sys.argv[1] if len(sys.argv) > 1 else 'meshes'
    os.makedirs(out_dir, exist_ok=True)
    valid_cube(out_dir)
    inverted_normals(out_dir)
    open_surface(out_dir)
    duplicate_vertices(out_dir)
    degenerate_triangles(out_dir)
    non_manifold_edge(out_dir)
    self_intersecting(out_dir)
    print(f"Generated 7 test meshes in {out_dir}/")

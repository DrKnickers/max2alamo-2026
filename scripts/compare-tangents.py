#!/usr/bin/env python3
"""
Option-4 empirical test: do vanilla per-vertex tangent vectors match what a
modern tangent-generation algorithm (Lengyel 2001 -- a close cousin of
MikkTSpace) would produce for the same geometry?

If yes (within tight tolerance), max2alamo can use Max 2026's built-in
tangent computation (MikkT) and the result will look like vanilla content
in-game. If no, we need to port the original max2alamo (Max 8) tangent
algorithm to match vanilla bit-exactly.

Parses a vanilla .alo chunk tree, extracts every submesh's per-vertex
(pos, normal, uv, tangent, binormal) plus the triangle index list,
computes Lengyel tangents for the same geometry, and reports per-submesh
statistics of the per-vertex angle between vanilla and computed.

Usage:
    python scripts/compare-tangents.py <path-to-vanilla.alo> [<more.alo> ...]

Vertex layout (rev 2, 144 B/vertex) per alamo_format/src/alo_build.cpp:
    off  0: pos.xyz     (12B)
    off 12: normal.xyz  (12B)
    off 24: uv0.xy      ( 8B)
    off 32: uv1.xy      ( 8B)
    off 40: uv2.xy      ( 8B)
    off 48: uv3.xy      ( 8B)
    off 56: tangent.xyz (12B)
    off 68: binormal.xyz(12B)
    off 80: color.rgb   (12B)
    off 92: alpha       ( 4B)
    off 96: unused      (16B)
    off112: boneIdx[4]  (16B)
    off128: weight[4]   (16B)
"""
import math
import os
import struct
import sys
from dataclasses import dataclass, field

# ---------- chunk-tree parser ----------------------------------------------

CONTAINER_BIT = 0x80000000

def read_chunk(data: bytes, pos: int):
    """Read one chunk header at `pos`, return (chunk_id_no_container_bit,
    is_container, payload_offset, payload_size, next_pos)."""
    if pos + 8 > len(data):
        raise EOFError(f"truncated chunk header at {pos}")
    cid, size_word = struct.unpack_from("<II", data, pos)
    is_container = bool(size_word & CONTAINER_BIT)
    size = size_word & ~CONTAINER_BIT
    payload_off = pos + 8
    payload_end = payload_off + size
    if payload_end > len(data):
        raise EOFError(f"chunk 0x{cid:x} at {pos}: payload runs past file")
    return cid, is_container, payload_off, size, payload_end


def walk_top_level(data: bytes):
    """Yield top-level chunks. Each tuple: (cid, is_container, payload_off,
    size, payload_end)."""
    pos = 0
    while pos < len(data):
        cid, container, off, size, end = read_chunk(data, pos)
        yield cid, container, off, size, end
        pos = end


def walk_children(data: bytes, payload_off: int, payload_end: int):
    pos = payload_off
    while pos < payload_end:
        cid, container, off, size, end = read_chunk(data, pos)
        yield cid, container, off, size, end
        pos = end


# ---------- per-submesh extraction ------------------------------------------

@dataclass
class Submesh:
    mesh_name: str
    shader_name: str
    positions: list = field(default_factory=list)   # list of (x,y,z)
    normals: list   = field(default_factory=list)
    uvs: list       = field(default_factory=list)   # (u,v)
    tangents: list  = field(default_factory=list)   # vanilla
    binormals: list = field(default_factory=list)   # vanilla
    indices: list   = field(default_factory=list)   # flat list of u16


def parse_vertex_record(buf: bytes, off: int):
    px, py, pz = struct.unpack_from("<fff", buf, off + 0)
    nx, ny, nz = struct.unpack_from("<fff", buf, off + 12)
    u,  v      = struct.unpack_from("<ff",  buf, off + 24)
    tx, ty, tz = struct.unpack_from("<fff", buf, off + 56)
    bx, by, bz = struct.unpack_from("<fff", buf, off + 68)
    return (px, py, pz), (nx, ny, nz), (u, v), (tx, ty, tz), (bx, by, bz)


def extract_submeshes(data: bytes):
    """Return [Submesh, ...] for all 0x400 meshes in the file whose 0x10000
    geometry uses rev-2 144B vertex format."""
    out = []
    for cid, is_container, off, size, end in walk_top_level(data):
        if cid != 0x400 or not is_container:
            continue
        mesh_name = "?"
        # First pass: find mesh name + indexed children
        materials = []   # list of (shader_name,)
        geometries = []  # list of (vertices_payload, vertices_size, indices_payload, indices_size)
        for ccid, ccontainer, coff, csize, cend in walk_children(data, off, end):
            if ccid == 0x401 and not ccontainer:
                # cstring, NUL-terminated
                raw = data[coff:coff + csize]
                mesh_name = raw.rstrip(b"\x00").decode("ascii", errors="replace")
            elif ccid == 0x10100 and ccontainer:
                shader = "?"
                for gcid, gc, goff, gsize, gend in walk_children(data, coff, cend):
                    if gcid == 0x10101 and not gc:
                        raw = data[goff:goff + gsize]
                        shader = raw.rstrip(b"\x00").decode("ascii", errors="replace")
                materials.append((shader,))
            elif ccid == 0x10000 and ccontainer:
                vbuf_off = vbuf_size = ibuf_off = ibuf_size = None
                vcount = fcount = 0
                for gcid, gc, goff, gsize, gend in walk_children(data, coff, cend):
                    if gcid == 0x10001:
                        vcount, fcount = struct.unpack_from("<II", data, goff)
                    elif gcid == 0x10007:
                        vbuf_off, vbuf_size = goff, gsize
                    elif gcid == 0x10004:
                        ibuf_off, ibuf_size = goff, gsize
                geometries.append((vcount, fcount, vbuf_off, vbuf_size, ibuf_off, ibuf_size))

        # Pair material[i] with geometry[i] (vanilla layout)
        for i, (vcount, fcount, voff, vsize, ioff, isize) in enumerate(geometries):
            shader = materials[i][0] if i < len(materials) else "?"
            if voff is None or ioff is None:
                continue
            # Only rev-2 144B geometry has tangents at the documented offset.
            if vsize != vcount * 144:
                continue
            sm = Submesh(mesh_name=mesh_name, shader_name=shader)
            for k in range(vcount):
                p, n, uv, t, b = parse_vertex_record(data, voff + k * 144)
                sm.positions.append(p); sm.normals.append(n)
                sm.uvs.append(uv); sm.tangents.append(t); sm.binormals.append(b)
            for k in range(fcount * 3):
                idx, = struct.unpack_from("<H", data, ioff + k * 2)
                sm.indices.append(idx)
            out.append(sm)
    return out


# ---------- Lengyel 2001 tangent computation --------------------------------

def vsub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def vadd(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def vmul(a, s): return (a[0]*s, a[1]*s, a[2]*s)
def vdot(a, b): return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
def vcross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def vlen(a): return math.sqrt(vdot(a, a))
def vnorm(a, eps=1e-12):
    L = vlen(a)
    return (a[0]/L, a[1]/L, a[2]/L) if L > eps else (0.0, 0.0, 0.0)


def lengyel_tangents(positions, normals, uvs, indices):
    """Lengyel 2001, "Computing Tangent Space Basis Vectors for an Arbitrary
    Mesh." Returns (tangents, binormals) parallel to `positions`."""
    n = len(positions)
    tan1 = [(0.0, 0.0, 0.0)] * n
    tan2 = [(0.0, 0.0, 0.0)] * n
    for t in range(0, len(indices), 3):
        i0, i1, i2 = indices[t], indices[t+1], indices[t+2]
        v0, v1, v2 = positions[i0], positions[i1], positions[i2]
        w0, w1, w2 = uvs[i0], uvs[i1], uvs[i2]
        x1 = v1[0]-v0[0]; x2 = v2[0]-v0[0]
        y1 = v1[1]-v0[1]; y2 = v2[1]-v0[1]
        z1 = v1[2]-v0[2]; z2 = v2[2]-v0[2]
        s1 = w1[0]-w0[0]; s2 = w2[0]-w0[0]
        u1 = w1[1]-w0[1]; u2 = w2[1]-w0[1]
        det = s1*u2 - s2*u1
        if abs(det) < 1e-12:
            continue
        r = 1.0 / det
        sdir = ((u2*x1 - u1*x2)*r, (u2*y1 - u1*y2)*r, (u2*z1 - u1*z2)*r)
        tdir = ((s1*x2 - s2*x1)*r, (s1*y2 - s2*y1)*r, (s1*z2 - s2*z1)*r)
        for i in (i0, i1, i2):
            tan1[i] = vadd(tan1[i], sdir)
            tan2[i] = vadd(tan2[i], tdir)

    tangents  = [(0.0, 0.0, 0.0)] * n
    binormals = [(0.0, 0.0, 0.0)] * n
    for i in range(n):
        nrm = normals[i]
        t   = tan1[i]
        # Gram-Schmidt
        ortho = vsub(t, vmul(nrm, vdot(nrm, t)))
        T = vnorm(ortho)
        # Handedness
        sign = -1.0 if vdot(vcross(nrm, t), tan2[i]) < 0.0 else 1.0
        # Binormal as cross(N, T) * sign — matches the typical convention.
        B = vmul(vcross(nrm, T), sign)
        tangents[i]  = T
        binormals[i] = B
    return tangents, binormals


# ---------- comparison ------------------------------------------------------

def angle_deg(a, b):
    la, lb = vlen(a), vlen(b)
    if la < 1e-6 or lb < 1e-6:
        return float("nan")
    c = max(-1.0, min(1.0, vdot(a, b) / (la * lb)))
    return math.degrees(math.acos(c))


def percentile(sorted_vals, p):
    if not sorted_vals: return float("nan")
    k = max(0, min(len(sorted_vals) - 1, int(round(p/100.0 * (len(sorted_vals)-1)))))
    return sorted_vals[k]


def analyse(path: str):
    with open(path, "rb") as f:
        data = f.read()
    submeshes = extract_submeshes(data)
    keep_shaders = ("MeshBumpColorize", "RSkinBumpColorize",
                    "MeshBumpReflectColorize", "RSkinBumpReflectColorize",
                    "TerrainMeshBump", "Tree", "Planet")
    relevant = [sm for sm in submeshes
                if any(sm.shader_name.startswith(p) for p in keep_shaders)]
    print(f"\n{'='*72}\n{os.path.basename(path)}: "
          f"{len(submeshes)} submesh(es), {len(relevant)} tangent-using\n{'='*72}")
    if not relevant:
        return

    file_t_angles = []
    file_b_angles = []
    for sm in relevant:
        T_van = sm.tangents
        B_van = sm.binormals
        T_len, B_len = lengyel_tangents(sm.positions, sm.normals, sm.uvs, sm.indices)
        # Stats over vertices that actually have a non-zero vanilla tangent
        # (some submeshes may have zeroed slots due to format quirks).
        per_vert_t = []
        per_vert_b = []
        for tv, tc, bv, bc in zip(T_van, T_len, B_van, B_len):
            if vlen(tv) < 1e-4 or vlen(tc) < 1e-4:
                continue
            per_vert_t.append(angle_deg(tv, tc))
            per_vert_b.append(angle_deg(bv, bc))
        per_vert_t.sort()
        per_vert_b.sort()
        n = len(per_vert_t)
        if n == 0:
            print(f"  {sm.mesh_name:32s} {sm.shader_name:28s} "
                  f"verts={len(sm.positions):5d}  (no valid tangents)")
            continue
        mean_t = sum(per_vert_t)/n; mean_b = sum(per_vert_b)/n
        max_t = per_vert_t[-1];     max_b = per_vert_b[-1]
        p95_t = percentile(per_vert_t, 95)
        p95_b = percentile(per_vert_b, 95)
        within5_t  = sum(1 for x in per_vert_t if x <= 5.0) / n * 100
        within15_t = sum(1 for x in per_vert_t if x <= 15.0) / n * 100
        print(f"  {sm.mesh_name[:28]:28s} {sm.shader_name:28s} verts={n:5d}")
        print(f"      tangent  angle: mean {mean_t:6.2f}deg  p95 {p95_t:6.2f}deg  "
              f"max {max_t:6.2f}deg  (within 5deg: {within5_t:5.1f}%, within 15deg: {within15_t:5.1f}%)")
        print(f"      binormal angle: mean {mean_b:6.2f}deg  p95 {p95_b:6.2f}deg  max {max_b:6.2f}deg")
        file_t_angles.extend(per_vert_t)
        file_b_angles.extend(per_vert_b)

    if file_t_angles:
        file_t_angles.sort(); file_b_angles.sort()
        n = len(file_t_angles)
        print(f"\n  FILE TOTAL  verts={n}")
        print(f"      tangent  mean {sum(file_t_angles)/n:.2f}deg  "
              f"p50 {percentile(file_t_angles, 50):.2f}deg  "
              f"p95 {percentile(file_t_angles, 95):.2f}deg  "
              f"max {file_t_angles[-1]:.2f}deg")
        print(f"      binormal mean {sum(file_b_angles)/n:.2f}deg  "
              f"p50 {percentile(file_b_angles, 50):.2f}deg  "
              f"p95 {percentile(file_b_angles, 95):.2f}deg  "
              f"max {file_b_angles[-1]:.2f}deg")
        return file_t_angles, file_b_angles
    return [], []


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    all_t = []
    all_b = []
    for path in sys.argv[1:]:
        result = analyse(path)
        if result:
            t, b = result
            all_t.extend(t); all_b.extend(b)
    if all_t:
        all_t.sort(); all_b.sort()
        n = len(all_t)
        print(f"\n{'='*72}\nGRAND TOTAL across {len(sys.argv)-1} files: "
              f"{n} vertices compared\n{'='*72}")
        print(f"  tangent  mean {sum(all_t)/n:.2f}deg  "
              f"p50 {percentile(all_t, 50):.2f}deg  "
              f"p95 {percentile(all_t, 95):.2f}deg  "
              f"max {all_t[-1]:.2f}deg")
        print(f"  binormal mean {sum(all_b)/n:.2f}deg  "
              f"p50 {percentile(all_b, 50):.2f}deg  "
              f"p95 {percentile(all_b, 95):.2f}deg  "
              f"max {all_b[-1]:.2f}deg")
        within1   = sum(1 for x in all_t if x <= 1.0)   / n * 100
        within5   = sum(1 for x in all_t if x <= 5.0)   / n * 100
        within15  = sum(1 for x in all_t if x <= 15.0)  / n * 100
        within45  = sum(1 for x in all_t if x <= 45.0)  / n * 100
        print(f"  tangent: within 1deg {within1:5.1f}%,  "
              f"within 5deg {within5:5.1f}%,  "
              f"within 15deg {within15:5.1f}%,  "
              f"within 45deg {within45:5.1f}%")


if __name__ == "__main__":
    main()

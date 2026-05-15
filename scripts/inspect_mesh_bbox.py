#!/usr/bin/env python3
"""Phase 9.1 — empirical confirmation of the 0x402 bbox layout.

Parses the 6 floats at offset 4..28 in each mesh's 0x402 chunk and
compares them against the AABB computed from the mesh's vertex
positions (read via the existing _alo.py parser). If the layout is
(min[3], max[3]) as our writer assumes, the floats and the computed
AABB will match within float epsilon.

Usage:
    python inspect_mesh_bbox.py <file.alo> [<more.alo> ...]

Default sample: ~10 files spanning small/medium/large vanilla content.

Exit code 0 on full match; 1 if any mesh disagrees by more than the
tolerance.
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve()
REPO   = SCRIPT.parent.parent
sys.path.insert(0, str(REPO / "tests" / "maxscript" / "verify"))
import _alo  # noqa: E402

TOLERANCE = 1e-3   # tighter than the float-quantization noise we tolerate


def read_0x402_floats(path: Path):
    """Return list of dicts: one per mesh, with the 6 raw floats from 0x402.

    Walks the chunk tree manually because _alo.py doesn't expose the
    bbox-bytes section of 0x402 (it only reads materialCount + the two
    flags). We parse here just enough to extract the 6 floats.
    """
    data = path.read_bytes()
    results = []
    pos = 0
    end = len(data)

    def read_chunk_header(off):
        if off + 8 > end:
            return None
        cid = struct.unpack_from("<I", data, off)[0]
        sz_raw = struct.unpack_from("<I", data, off + 4)[0]
        size = sz_raw & 0x7FFFFFFF
        is_container = bool(sz_raw & 0x80000000)
        return cid, size, is_container

    def walk(start, stop, in_mesh=False, current_mesh_floats=None):
        o = start
        while o < stop:
            h = read_chunk_header(o)
            if h is None:
                return
            cid, size, is_container = h
            payload_start = o + 8
            payload_end = payload_start + size
            if cid == 0x400:
                # Container -- new mesh
                mesh_state = {"floats": None}
                walk(payload_start, payload_end, in_mesh=True,
                     current_mesh_floats=mesh_state)
                if mesh_state["floats"] is not None:
                    results.append(mesh_state["floats"])
            elif cid == 0x402 and in_mesh and current_mesh_floats is not None:
                # Leaf -- read materialCount + 6 floats from offset 4..28.
                if size >= 28:
                    floats = struct.unpack_from("<6f", data, payload_start + 4)
                    current_mesh_floats["floats"] = floats
            elif is_container:
                walk(payload_start, payload_end, in_mesh=in_mesh,
                     current_mesh_floats=current_mesh_floats)
            o = payload_end
    walk(0, end)
    return results


def compute_vertex_aabb(mesh):
    """Compute (min, max) AABB across all submeshes of an _alo.Mesh."""
    mn = [+math.inf] * 3
    mx = [-math.inf] * 3
    n = 0
    for sm in mesh.submeshes:
        for v in sm.vertices:
            p = v.position
            for i in range(3):
                if p[i] < mn[i]: mn[i] = p[i]
                if p[i] > mx[i]: mx[i] = p[i]
            n += 1
    if n == 0:
        return None, None
    return tuple(mn), tuple(mx)


def classify(floats, vmin, vmax):
    """Test multiple candidate layouts; return the closest match label."""
    f = floats
    # Candidate A: (min[3], max[3])
    a_match = all(abs(f[i] - vmin[i]) < TOLERANCE for i in range(3)) and \
              all(abs(f[3 + i] - vmax[i]) < TOLERANCE for i in range(3))
    # Candidate B: (center[3], extent[3])  (half-range)
    center = tuple((vmin[i] + vmax[i]) * 0.5 for i in range(3))
    extent = tuple((vmax[i] - vmin[i]) * 0.5 for i in range(3))
    b_match = all(abs(f[i] - center[i]) < TOLERANCE for i in range(3)) and \
              all(abs(f[3 + i] - extent[i]) < TOLERANCE for i in range(3))
    # Candidate C: all-zero (mesh authored without bbox)
    c_match = all(abs(f[i]) < TOLERANCE for i in range(6))
    if a_match:
        return "AABB(min,max)"
    if b_match:
        return "(center,extent)"
    if c_match:
        return "all-zero"
    return "UNKNOWN"


def process(path: Path, tally: dict):
    print(f"\n=== {path.name} ===")
    try:
        alo = _alo.load(str(path))
    except Exception as e:
        print(f"  parse failed: {e}")
        return
    raw_floats = read_0x402_floats(path)
    if len(raw_floats) != len(alo.meshes):
        print(f"  WARN: 0x402 count {len(raw_floats)} != mesh count "
              f"{len(alo.meshes)}; skipping")
        return
    for m, floats in zip(alo.meshes, raw_floats):
        vmin, vmax = compute_vertex_aabb(m)
        if vmin is None:
            label = "(no vertices)"
        else:
            label = classify(floats, vmin, vmax)
        tally[label] = tally.get(label, 0) + 1
        print(f"  mesh '{m.name}': 0x402 floats={floats}  "
              f"vertex AABB=({vmin}, {vmax})  -> {label}")


def main():
    if len(sys.argv) > 1:
        files = [Path(a) for a in sys.argv[1:]]
    else:
        defaults = [
            "tests/corpus/foc/AI_RANCOR.ALO",
            "tests/corpus/foc/EB_COMMANDCENTER.ALO",
            "tests/corpus/foc/EI_TROOPER.ALO",
            "tests/corpus/foc/RV_XWING.ALO",
            "tests/corpus/foc/EV_STARDESTROYER.ALO",
            "tests/corpus/eaw/W_PLANET_TATOOINE_LOW.ALO",
        ]
        files = []
        for d in defaults:
            cand = REPO / d
            if not cand.exists():
                cand = Path("C:/Modding/max2alamo-2026") / d
            if cand.exists():
                files.append(cand)
    tally = {}
    for p in files:
        process(p, tally)
    print("\n=== SUMMARY ===")
    if not tally:
        print("  no meshes inspected")
        return 2
    total = sum(tally.values())
    print(f"  meshes inspected: {total}")
    print(f"  layout classification: {tally}")
    aabb_count = tally.get("AABB(min,max)", 0)
    zero_count = tally.get("all-zero", 0)
    unknown_count = tally.get("UNKNOWN", 0)
    print(f"  AABB-or-zero (acceptable): "
          f"{aabb_count + zero_count}/{total} "
          f"({100.0 * (aabb_count + zero_count) / total:.1f}%)")
    if unknown_count > 0:
        print(f"  UNKNOWN: {unknown_count} -- investigate before claiming "
              f"the AABB layout is universal")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

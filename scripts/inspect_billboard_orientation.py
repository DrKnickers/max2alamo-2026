#!/usr/bin/env python3
"""Phase 12.1 — empirical check of the Alamo billboard pivot convention.

For each mesh in a .alo whose attached bone has `billboard_mode != 0`,
compute the average face normal in the mesh's vertex-position space and
identify which axis it dominantly points along. If the engine's
authoring convention is "billboard face normal along bone-local -Y"
(per AloViewer source's BillboardCorrection), the vast majority of
vanilla billboard meshes should report a `-Y` dominant axis here.

Usage:
    python inspect_billboard_orientation.py <path/to/file.alo> [<more.alo> ...]

Or with no args: scans the EV_EXECUTORSTARDESTROYER + a few other
billboard-rich corpus files for a representative sample.
"""
from __future__ import annotations

import math
import sys
from collections import Counter
from pathlib import Path

SCRIPT = Path(__file__).resolve()
REPO   = SCRIPT.parent.parent
sys.path.insert(0, str(REPO / "tests" / "maxscript" / "verify"))
import _alo  # noqa: E402

# Match AloViewer's enum (src/General/GameTypes.h) for nicer reporting.
MODE_NAMES = {
    0: "DISABLE", 1: "PARALLEL", 2: "FACE", 3: "ZAXIS_VIEW",
    4: "ZAXIS_LIGHT", 5: "ZAXIS_WIND", 6: "SUNLIGHT_GLOW", 7: "SUN",
}


def cross(a, b):
    return (
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0],
    )


def sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def length(v): return math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])
def normalize(v):
    L = length(v)
    return (v[0]/L, v[1]/L, v[2]/L) if L > 1e-9 else (0.0, 0.0, 0.0)
def add(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])


def axis_label(v):
    """Map a (near-)unit vector to its dominant axis label (+X / -Y / etc.)
    plus a 'tightness' score (1.0 = perfectly axis-aligned, 0.577 = the
    body-diagonal worst case)."""
    ax = max(range(3), key=lambda i: abs(v[i]))
    sign = "+" if v[ax] >= 0 else "-"
    name = "XYZ"[ax]
    tight = abs(v[ax])  # cosine of angle to nearest axis
    return f"{sign}{name}", tight


def average_face_normal(submesh):
    """Compute the area-weighted average face normal in vertex-position
    space (which is bone-local for vanilla content -- mesh vertices in
    the .alo are pre-baked into their attached bone's frame)."""
    n = (0.0, 0.0, 0.0)
    nverts = len(submesh.vertices)
    if nverts == 0:
        return None, 0
    idx = submesh.indices
    tris_used = 0
    for i in range(0, len(idx) - 2, 3):
        ia, ib, ic = idx[i], idx[i+1], idx[i+2]
        if ia >= nverts or ib >= nverts or ic >= nverts:
            continue
        a = submesh.vertices[ia].position
        b = submesh.vertices[ib].position
        c = submesh.vertices[ic].position
        # cross(b-a, c-a) -- magnitude == 2 * triangle area, so this is
        # naturally area-weighted. degenerate triangles contribute zero.
        nrm = cross(sub(b, a), sub(c, a))
        if length(nrm) < 1e-12:
            continue
        n = add(n, nrm)
        tris_used += 1
    if tris_used == 0:
        return None, 0
    return normalize(n), tris_used


def report(path: Path, summary: Counter, per_mode_summary: dict):
    print(f"\n=== {path.name} ===")
    try:
        alo = _alo.load(str(path))
    except Exception as e:
        print(f"  parse failed: {e}")
        return

    # Map mesh-or-light object index to bone via connections.
    # Connection object_index < len(meshes) -> meshes[oi].
    bone_for_mesh = {}
    for c in alo.connections:
        if c.object_index < len(alo.meshes):
            bone_for_mesh[c.object_index] = c.bone_index

    count_in_file = 0
    for mi, m in enumerate(alo.meshes):
        b_idx = bone_for_mesh.get(mi)
        if b_idx is None or b_idx >= len(alo.bones):
            continue
        b = alo.bones[b_idx]
        if b.billboard_mode == 0:
            continue
        # Average face normal across all submeshes of this mesh.
        agg = (0.0, 0.0, 0.0)
        tris_total = 0
        for sm in m.submeshes:
            n, tris = average_face_normal(sm)
            if n is None:
                continue
            # Re-weight by triangle count (rough proxy for surface area).
            agg = add(agg, (n[0]*tris, n[1]*tris, n[2]*tris))
            tris_total += tris
        if tris_total == 0:
            continue
        nrm = normalize(agg)
        label, tight = axis_label(nrm)
        mode_name = MODE_NAMES.get(b.billboard_mode, f"?{b.billboard_mode}")
        print(f"  mesh '{m.name}' bone '{b.name}' mode={mode_name}: "
              f"avg normal = ({nrm[0]:+.3f}, {nrm[1]:+.3f}, {nrm[2]:+.3f}) "
              f"-> {label}  (tightness {tight:.3f}, {tris_total} tris)")
        summary[label] += 1
        per_mode_summary.setdefault(b.billboard_mode, Counter())[label] += 1
        count_in_file += 1
    if count_in_file == 0:
        print("  (no billboard meshes)")


def main():
    if len(sys.argv) > 1:
        files = [Path(a) for a in sys.argv[1:]]
    else:
        # Default sample: known billboard-rich vanilla files.
        defaults = [
            "tests/corpus/foc/EV_EXECUTORSTARDESTROYER.ALO",
            "tests/corpus/foc/EV_EXECUTORSTARDESTROYER_HP_R_TRB03.ALO",
            "tests/corpus/foc/AI_RANCOR.ALO",
            "tests/corpus/foc/EV_STARDESTROYER.ALO",
            "tests/corpus/foc/EV_VENATOR.ALO",
        ]
        # Resolve against worktree first; fall back to main checkout if absent.
        files = []
        for d in defaults:
            cand = REPO / d
            if not cand.exists():
                cand = Path("C:/Modding/max2alamo-2026") / d
            if cand.exists():
                files.append(cand)

    overall = Counter()
    per_mode = {}
    for p in files:
        report(p, overall, per_mode)

    print("\n=== SUMMARY ===")
    if not overall:
        print("  no billboard meshes inspected")
        return
    total = sum(overall.values())
    print(f"  billboard meshes inspected: {total}")
    print(f"  axis distribution: {dict(overall)}")
    top_axis, top_count = overall.most_common(1)[0]
    print(f"  dominant axis: {top_axis} ({top_count}/{total} = "
          f"{100.0*top_count/total:.1f}%)")
    print()
    print("  per-mode breakdown:")
    for mode in sorted(per_mode):
        print(f"    {MODE_NAMES.get(mode, mode)}: {dict(per_mode[mode])}")


if __name__ == "__main__":
    main()

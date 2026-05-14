#!/usr/bin/env python3
"""Phase 12 — corpus non-regression check for the shadow-volume validator.

For every .alo in tests/corpus/{eaw,foc}/, identify submeshes whose
shader is MeshShadowVolume.fx or RSkinShadowVolume.fx, run an equivalent
Python implementation of the closed-volume check, and report the
pass rate. A high pass rate (>= 95%) means our validator agrees with
the legacy Petroglyph exporter's expectation that authored vanilla
shadow meshes are closed manifolds.

Run from anywhere; resolves the repo root from this script's location.

Exit codes:
  0 = pass rate >= threshold (default 95%)
  1 = pass rate below threshold (regression or algorithm disagreement)
  2 = setup/usage error
"""
from __future__ import annotations

import argparse
import math
import os
import sys
from pathlib import Path


# Pull _alo.py from tests/maxscript/verify/ -- it knows how to read
# the typed .alo we write (and which vanilla content also conforms to).
SCRIPT = Path(__file__).resolve()
REPO   = SCRIPT.parent.parent
sys.path.insert(0, str(REPO / "tests" / "maxscript" / "verify"))
import _alo  # noqa: E402

SHADOW_SHADERS = {"MeshShadowVolume.fx", "RSkinShadowVolume.fx"}
EPS = 1e-5  # match the walker's position-dedup tolerance


def quantize(p):
    return (round(p[0] / EPS), round(p[1] / EPS), round(p[2] / EPS))


def position_dedup(verts):
    """Map raw-vertex-index -> position-compact-index. Returns the remap list."""
    pos_to_idx = {}
    remap = []
    for v in verts:
        k = quantize(v.position)
        if k not in pos_to_idx:
            pos_to_idx[k] = len(pos_to_idx)
        remap.append(pos_to_idx[k])
    return remap


def closed_volume_check(verts, indices):
    """Returns (is_closed, non_manifold_edge_count, tri_count, status).

    status is "ok", "no_verts" (submesh shape we can't validate), or
    "out_of_range" (index >= len(verts) -- malformed or different layout).
    """
    if not verts:
        return False, 0, 0, "no_verts"
    nv = len(verts)
    remap = position_dedup(verts)
    edge_count = {}
    tris = 0
    for i in range(0, len(indices) - 2, 3):
        ia, ib, ic = indices[i], indices[i+1], indices[i+2]
        if ia >= nv or ib >= nv or ic >= nv:
            return False, 0, 0, "out_of_range"
        a, b, c = remap[ia], remap[ib], remap[ic]
        if a == b or b == c or a == c:  # degenerate skip
            continue
        tris += 1
        for (x, y) in ((a, b), (b, c), (c, a)):
            k = (min(x, y), max(x, y))
            edge_count[k] = edge_count.get(k, 0) + 1
    bad = sum(1 for cnt in edge_count.values() if cnt != 2)
    return bad == 0, bad, tris, "ok"


def shadow_submeshes(alo):
    out = []
    for m in alo.meshes:
        for s in m.submeshes:
            if s.shader_name in SHADOW_SHADERS:
                out.append((m.name, s))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--threshold", type=float, default=75.0,
                    help="minimum pass-rate percent (default 75). "
                         "Set well below the observed baseline (~86% on the "
                         "first 200 EaW/FoC vanilla files, 2026-05) so this "
                         "is a non-regression alarm, not a quality gate. "
                         "Vanilla content has real authoring noise -- meshes "
                         "with 1-4 non-manifold edges over hundreds of "
                         "triangles -- which the legacy PG exporter also "
                         "warned on. The validator agreeing with PG is what "
                         "matters, not the absolute rate.")
    ap.add_argument("--limit", type=int, default=0,
                    help="max .alo files to scan per game (0 = all)")
    ap.add_argument("--verbose", action="store_true",
                    help="print every failing mesh, not just the summary")
    args = ap.parse_args()

    # Corpus is gitignored and lives only in the main checkout. From a
    # worktree, fall back to the main checkout's path if the local
    # tests/corpus is empty.
    candidates = [
        REPO / "tests" / "corpus" / "eaw",
        REPO / "tests" / "corpus" / "foc",
        Path("C:/Modding/max2alamo-2026/tests/corpus/eaw"),
        Path("C:/Modding/max2alamo-2026/tests/corpus/foc"),
    ]
    # De-dup by resolved path, preserve order.
    seen, corpus_dirs = set(), []
    for d in candidates:
        r = d.resolve() if d.exists() else d
        if r not in seen:
            seen.add(r)
            corpus_dirs.append(d)
    files = []
    for d in corpus_dirs:
        if not d.is_dir():
            print(f"  WARN: corpus dir missing: {d}", file=sys.stderr)
            continue
        # Vanilla files use .ALO uppercase; some test artefacts use .alo.
        these = sorted(list(d.glob("*.ALO")) + list(d.glob("*.alo")))
        if args.limit > 0:
            these = these[:args.limit]
        files.extend(these)

    if not files:
        print("ERROR: no corpus files found", file=sys.stderr)
        return 2

    total_meshes = 0
    closed_meshes = 0
    skipped = {"no_verts": 0, "out_of_range": 0}
    failures = []
    files_with_shadow = 0

    for path in files:
        try:
            alo = _alo.load(str(path))
        except Exception as e:
            print(f"  WARN: failed to parse {path.name}: {e}", file=sys.stderr)
            continue
        subs = shadow_submeshes(alo)
        if subs:
            files_with_shadow += 1
        for mesh_name, sm in subs:
            is_closed, bad, tris, status = closed_volume_check(sm.vertices, sm.indices)
            if status != "ok":
                skipped[status] = skipped.get(status, 0) + 1
                continue
            total_meshes += 1
            if is_closed:
                closed_meshes += 1
            else:
                failures.append((path.name, mesh_name, sm.shader_name, bad, tris))

    if total_meshes == 0:
        print("ERROR: corpus contained zero shadow-volume submeshes; check fixtures",
              file=sys.stderr)
        return 2

    pass_rate = 100.0 * closed_meshes / total_meshes

    print(f"Scanned {len(files):,} .alo files; {files_with_shadow:,} contained "
          f"shadow-volume meshes.")
    print(f"Shadow-volume submeshes inspected: {total_meshes:,}  "
          f"(skipped: {skipped})")
    print(f"Closed: {closed_meshes:,}   Open: {total_meshes - closed_meshes:,}")
    print(f"Pass rate: {pass_rate:.2f}%   (threshold {args.threshold:.1f}%)")

    if args.verbose and failures:
        print("\nFirst 20 failing meshes:")
        for f, mn, sn, bad, tris in failures[:20]:
            print(f"  {f}::{mn} [{sn}]  -> {bad} non-manifold edges over {tris} tris")

    if pass_rate < args.threshold:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

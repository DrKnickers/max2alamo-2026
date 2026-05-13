"""verify_test_pivot_skinned_safety.py - Phase 5g skinning regression guard.

Confirms that composing object offset into the bone matrix doesn't break
Skin modifier vertex evaluation. Setup: 2-bone chain (B0_Pivoted with
-90deg-Z offset, B1 with identity offset); cylinder skinned with rigid
per-row binding.

Pinned:
  - B0_Pivoted carries the offset (col1 != (0,1,0))
  - B1 has identity-ish rotation (col1 ~ (0,1,0))
  - SkinCyl has 1 submesh, the expected vertex count, weight-sum=1
  - Bone indices on every vertex resolve to existing bones
  - No NaN/Inf in any vertex field

If this verifier fails after the Phase 5g fix lands, skinning has
regressed and we need a per-call distinction between hardpoint and
skinning bone-matrix conventions.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo

_TOL_DIR = 1e-3
_TOL_W   = 1e-3
_IDENTITY_Y = (0.0, 1.0, 0.0)


def _within(v, expected, tol):
    return all(abs(v[i] - expected[i]) < tol for i in range(3))


def _is_finite(x):
    return not (math.isnan(x) or math.isinf(x))


def main(alo_path):
    errors = []
    a = _alo.load(alo_path)
    by_name = {b.name: b for b in a.bones}

    # B0_Pivoted carries the offset; col1 should NOT match identity.
    b0 = by_name.get("B0_Pivoted")
    if b0 is None:
        errors.append("#1 B0_Pivoted missing")
    else:
        col1 = (b0.matrix[1], b0.matrix[5], b0.matrix[9])
        if _within(col1, _IDENTITY_Y, _TOL_DIR):
            errors.append(f"#1 B0_Pivoted col1 = {col1}, expected NOT-identity "
                          f"(was the objectoffsetrot composed in?)")

    # B1 has no offset; col1 should be ~(0,1,0).
    b1 = by_name.get("B1")
    if b1 is None:
        errors.append("#2 B1 missing")
    else:
        col1 = (b1.matrix[1], b1.matrix[5], b1.matrix[9])
        if not _within(col1, _IDENTITY_Y, _TOL_DIR):
            errors.append(f"#2 B1 col1 = {col1}, expected ~{_IDENTITY_Y} "
                          f"(no offset authored on B1)")

    # SkinCyl mesh present, one submesh, weight-sum invariant per vertex.
    cyl = a.mesh_by_name("SkinCyl")
    if cyl is None or not cyl.submeshes:
        errors.append("#3 SkinCyl missing or has no submesh")
    else:
        sm = cyl.submeshes[0]
        if len(sm.vertices) == 0:
            errors.append("#3 SkinCyl submesh has no vertices")
        for vi, v in enumerate(sm.vertices):
            # Weight sum == 1.0 (rigid binding -> exactly one slot active)
            wsum = sum(v.weights)
            if abs(wsum - 1.0) > _TOL_W:
                errors.append(f"#4 SkinCyl vert[{vi}] weight sum = {wsum:.5f}, "
                              f"expected 1.0")
                break
            # Every bone reference must resolve to a real bone.
            for slot in range(4):
                if v.weights[slot] > 0:
                    if v.bone_indices[slot] >= len(a.bones):
                        errors.append(f"#5 SkinCyl vert[{vi}] bone_indices[{slot}]="
                                      f"{v.bone_indices[slot]} out of range "
                                      f"(bones={len(a.bones)})")
                        break
            # No NaN/Inf in any float.
            floats = (list(v.position) + list(v.normal) + list(v.uv) +
                      list(v.tangent) + list(v.binormal) + list(v.color) +
                      [v.alpha] + list(v.weights))
            if any(not _is_finite(f) for f in floats):
                errors.append(f"#6 SkinCyl vert[{vi}] contains NaN/Inf")
                break

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    n_verts = len(cyl.submeshes[0].vertices) if (cyl and cyl.submeshes) else 0
    print(f"OK  (B0 offset captured + B1 identity preserved + SkinCyl "
          f"{n_verts} verts with valid bone refs and weight-sum=1)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

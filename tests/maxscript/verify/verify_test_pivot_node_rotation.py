"""verify_test_pivot_node_rotation.py - Phase 5g sanity guardrail.

Confirms that node-level rotation (b.rotation / d.rotation) propagates
into the on-disk bone matrix. This was already working before Phase 5g;
the test exists so any regression in the new compose_with_object_offset
helper that affects the node-TM path is caught.

Pinned:
  - Both BoneRotated and DummyRotated bones present
  - Each bone's matrix col1 (local +Y in parent) matches the authored
    eulerAngles 0 0 -90 rotation within 1e-3 (~ (-1, 0, 0))
  - Each bone's column basis is unit-length and pairwise orthogonal
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo

_TOL_DIR    = 1e-3   # tolerance on direction-axis components
_TOL_UNIT   = 1e-3   # tolerance on column unit length
_TOL_ORTHO  = 1e-3   # tolerance on pairwise dot product (orthogonality)


def _decode_basis(m):
    """Returns ((col0_x,col0_y,col0_z), (col1_*), (col2_*)) for a 12-float bone matrix."""
    x_axis = (m[0], m[4], m[8])
    y_axis = (m[1], m[5], m[9])
    z_axis = (m[2], m[6], m[10])
    return x_axis, y_axis, z_axis


def _len(v):
    return math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])


def _dot(a, b):
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]


def _within(v, expected, tol):
    return all(abs(v[i] - expected[i]) < tol for i in range(3))


def main(alo_path):
    errors = []
    a = _alo.load(alo_path)
    by_name = {b.name: b for b in a.bones}

    EXPECTED_COL1 = (-1.0, 0.0, 0.0)   # local +Y in parent for eulerAngles 0 0 -90

    for bone_name in ("BoneRotated", "DummyRotated"):
        if bone_name not in by_name:
            errors.append(f"#1 missing bone {bone_name!r}")
            continue
        b = by_name[bone_name]
        x_axis, y_axis, z_axis = _decode_basis(b.matrix)

        # #1 col1 matches authored direction within tolerance
        if not _within(y_axis, EXPECTED_COL1, _TOL_DIR):
            errors.append(f"#1 {bone_name} col1 = {y_axis}, "
                          f"expected ~{EXPECTED_COL1} within {_TOL_DIR}")

        # #2 column lengths are unit
        for axis_name, axis in (("col0", x_axis), ("col1", y_axis), ("col2", z_axis)):
            mag = _len(axis)
            if abs(mag - 1.0) > _TOL_UNIT:
                errors.append(f"#2 {bone_name} {axis_name} |.|={mag:.5f}, expected 1.0")

        # #3 columns pairwise orthogonal
        for (n1, a1), (n2, a2) in (
            (("col0", x_axis), ("col1", y_axis)),
            (("col0", x_axis), ("col2", z_axis)),
            (("col1", y_axis), ("col2", z_axis)),
        ):
            d = abs(_dot(a1, a2))
            if d > _TOL_ORTHO:
                errors.append(f"#3 {bone_name} {n1}.{n2} |dot|={d:.5f}, expected ~0")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  (BoneRotated + DummyRotated both rotated as authored "
          f"(col1 ~ (-1,0,0)); columns orthonormal)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

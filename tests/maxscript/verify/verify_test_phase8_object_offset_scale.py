"""verify_test_phase8_object_offset_scale.py - Phase 8f 5g caveat pinning.

Asserts that bones with non-identity objectoffsetscale still have
UNIT-LENGTH column vectors on disk -- proving Phase 5g's silent-drop
of the scale offset (it composes rot + pos, but not scale).

If a future change starts composing scale, the unit-length assertion
fires loudly.
"""
import math
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _alo


def col_length(matrix_12_floats, col):
    """matrix_12_floats is row-major flat [r0c0, r0c1, r0c2, ..., r3c2].
    Get column `col` length (3 floats: r0/r1/r2 at the same col).
    Actually .alo stores the matrix as 3-row x 4-col = 12 floats, but
    representation differs by reader. Use _alo.py's decode for safety.
    """
    pass


def main(alo_path):
    errors = []
    try:
        alo = _alo.load(alo_path)
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr); return 1

    for nm in ["HelperScaled", "HelperUniform"]:
        b = next((b for b in alo.bones if b.name == nm), None)
        if b is None:
            errors.append(f"#A bone {nm!r} not found")
            continue
        # matrix is 12 floats stored row-major: [c0[0..2], c1[0..2], c2[0..2], c3[0..2]]
        # Actually _alo.py stores them per the format. Check its representation.
        # Phase 4 layout (per format-notes): file stores columns: c[1..12] where
        # row1 = c[1],c[5],c[9]; row2 = c[2],c[6],c[10]; etc.
        # So the matrix in _alo is decoded as 4 rows of 3 (Matrix3) per Mike's
        # importer. _alo.Bone has a `matrix` field of 12 floats.
        if not hasattr(b, "matrix"):
            errors.append(f"#B {nm!r} has no .matrix field on the Bone object")
            continue
        m = b.matrix
        if len(m) < 12:
            errors.append(f"#B {nm!r} matrix has {len(m)} floats, expected >= 12")
            continue
        # Per docs/format-notes.md:107, the file stores:
        #   row1 = m[0], m[4], m[8]   (col-major reading the row's basis vec)
        #   row2 = m[1], m[5], m[9]
        #   row3 = m[2], m[6], m[10]
        #   row4 = m[3], m[7], m[11]  (translation)
        # The 3x3 basis is the first 3 rows; each row is a basis vector.
        # For unit-length check we look at row0/1/2:
        for r in range(3):
            x, y, z = m[r], m[4 + r], m[8 + r]
            L = math.sqrt(x * x + y * y + z * z)
            if abs(L - 1.0) > 1e-3:
                errors.append(f"#B {nm!r} row {r} length = {L:.4f}, expected 1.0 "
                              f"(objectoffsetscale should NOT compose into bone matrix -- "
                              f"if it did, the column would be scaled by {3 if nm=='HelperScaled' else 2})")
                break

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  (HelperScaled with offsetscale=[3,1,1] + HelperUniform with [2,2,2] "
          f"both have unit-length basis rows; 5g silent-drop pinned; assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

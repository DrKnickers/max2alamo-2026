"""verify_test_two_meshes_offset.py — regression for PR #17.

Two-mesh scene with each mesh at a distinct world position. The per-mesh
attachment bone must carry that mesh's WorldTM translation, otherwise
both meshes render stacked at the origin in AloViewer (the original
Phase 4c bug)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def approx(a, b, tol=1e-3):
    return abs(a - b) < tol


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # Expected: Root + 2 per-mesh bones = 3.
    if len(a.bones) != 3:
        errors.append(f"expected 3 bones (Root + BoxA + SphereB), got {len(a.bones)}")

    expected = {
        "BoxA":    (10.0, 0.0, 5.0),
        "SphereB": (-15.0, 20.0, 5.0),
    }
    for name, expected_pos in expected.items():
        b = a.bone_by_name(name)
        if b is None:
            errors.append(f"missing bone {name!r}")
            continue
        tx, ty, tz = b.translation
        ex, ey, ez = expected_pos
        if not (approx(tx, ex) and approx(ty, ey) and approx(tz, ez)):
            errors.append(f"bone {name!r} translation should be {expected_pos}, "
                          f"got ({tx}, {ty}, {tz})")
        # The rotation/scale rows should be identity for these
        # axis-aligned default-pivot primitives.
        identity = [1, 0, 0,  0, 1, 0,  0, 0, 1]  # 9 of the 12 floats (axes)
        actual_axes = [b.matrix[i] for i in (0, 4, 8,  1, 5, 9,  2, 6, 10)]
        for i, (a_val, e_val) in enumerate(zip(actual_axes, identity)):
            if not approx(a_val, e_val):
                errors.append(f"bone {name!r} axis float[{i}] should be {e_val} "
                              f"(no rotation), got {a_val}")
                break

    # Each mesh must connect to its own per-mesh bone.
    if len(a.connections) != 2:
        errors.append(f"expected 2 connections, got {len(a.connections)}")
    else:
        bone_indices = {c.bone_index for c in a.connections}
        if bone_indices != {1, 2}:
            errors.append(f"connections should point to bones {{1, 2}}, got {bone_indices}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (BoxA at {a.bone_by_name('BoxA').translation}, "
          f"SphereB at {a.bone_by_name('SphereB').translation})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

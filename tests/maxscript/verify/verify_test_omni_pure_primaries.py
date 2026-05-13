"""verify_test_omni_pure_primaries.py - Phase 7b.1 multi-light test.

Pins:
  - Three Omni lights export with the right colors in authoring order
  - Connection table has 3 entries at monotonic object_index 0/1/2
  - Per-light bones present in same order
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def approx(a, b, tol=1e-3):
    return abs(a - b) < tol


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    if len(a.lights) != 3:
        errors.append(f"expected 3 lights, got {len(a.lights)}: "
                      f"{[l.name for l in a.lights]}")
        _fail(errors); return 1

    # Expected order matches authoring: Red, Green, Blue.
    expected = [
        ("L_Red",   (1.0, 0.0, 0.0)),
        ("L_Green", (0.0, 1.0, 0.0)),
        ("L_Blue",  (0.0, 0.0, 1.0)),
    ]
    for i, (name, color) in enumerate(expected):
        l = a.lights[i]
        if l.name != name:
            errors.append(f"lights[{i}]: name should be {name!r}, got {l.name!r}")
        if l.type != 0:
            errors.append(f"lights[{i}] ({name}): type should be 0 (Omni), got {l.type}")
        for ci, want in enumerate(color):
            if not approx(l.color[ci], want):
                errors.append(f"lights[{i}] ({name}): color[{ci}] should be {want}, "
                              f"got {l.color[ci]:.4f}")

    # Connections: 3 lights, no meshes -> object_indices 0, 1, 2.
    if len(a.connections) != 3:
        errors.append(f"expected 3 connections, got {len(a.connections)}")
    else:
        for i in range(3):
            if a.connections[i].object_index != i:
                errors.append(f"connections[{i}].object_index should be {i}, "
                              f"got {a.connections[i].object_index}")

    # Per-light synthetic bones: Root + L_Red + L_Green + L_Blue.
    for name in ("L_Red", "L_Green", "L_Blue"):
        if a.bone_by_name(name) is None:
            errors.append(f"per-light bone {name!r} missing from skeleton")

    if errors: _fail(errors); return 1
    print("OK  (3 primary-color omni lights round-tripped in order)")
    return 0


def _fail(errors):
    print("FAIL:", file=sys.stderr)
    for e in errors:
        print(f"  - {e}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

"""verify_test_directional_light.py - Phase 7b.1 directional path.

Pins:
  - DirectionalLight -> ExportLight with type=1
  - color / intensity round-trip
  - synthetic bone at the light's world position
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

    if len(a.lights) != 1:
        errors.append(f"expected 1 light, got {len(a.lights)}")
    else:
        l = a.lights[0]
        if l.name != "DirLight":
            errors.append(f"name should be 'DirLight', got {l.name!r}")
        if l.type != 1:
            errors.append(f"type should be 1 (Directional), got {l.type}")
        # Color (rough tol; Max color->float quantization can drift up to
        # ~0.004 due to 0-255 int rounding)
        for ci, want in enumerate((0.8, 0.7, 0.6)):
            if not approx(l.color[ci], want, tol=0.01):
                errors.append(f"color[{ci}] should be ~{want}, got {l.color[ci]:.4f}")
        if not approx(l.intensity, 0.8):
            errors.append(f"intensity should be 0.8, got {l.intensity}")

    lb = a.bone_by_name("DirLight")
    if lb is None:
        errors.append("synthetic per-light bone 'DirLight' missing")
    else:
        tx, ty, tz = lb.translation
        if not (approx(tx, 0, tol=0.01) and approx(ty, 0, tol=0.01) and approx(tz, 100, tol=0.01)):
            errors.append(f"light bone translation should be (0,0,100), "
                          f"got ({tx}, {ty}, {tz})")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (DirectionalLight -> type=1, color and bone position round-tripped)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

"""verify_test_freespot_no_target.py - Phase 7b.2 no-target path.

Pins:
  - FreeSpot -> ExportLight type=2 (same chunk type as TargetSpot)
  - hotspot=20deg, falloff=60deg round-trip to radians
  - Walker emits ONLY the light's own bone, no '.Target' sibling
    (since INode::GetTarget() returns null for a FreeSpot)
  - Skeleton has exactly Root + FreeSpotLight, nothing more
"""
import math
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
        if l.name != "FreeSpotLight":
            errors.append(f"light name should be 'FreeSpotLight', got {l.name!r}")
        if l.type != 2:
            errors.append(f"type should be 2 (Spotlight), got {l.type}")
        if not approx(l.hotspot, math.radians(20.0), tol=1e-3):
            errors.append(f"hotspot should be ~{math.radians(20):.4f} (20deg), "
                          f"got {l.hotspot:.4f}")
        if not approx(l.falloff, math.radians(60.0), tol=1e-3):
            errors.append(f"falloff should be ~{math.radians(60):.4f} (60deg), "
                          f"got {l.falloff:.4f}")

    if a.bone_by_name("FreeSpotLight.Target") is not None:
        errors.append("FreeSpotLight.Target bone should NOT exist (FreeSpot has "
                      "no target node)")

    if a.bone_by_name("FreeSpotLight") is None:
        errors.append("FreeSpotLight bone missing -- walker didn't emit the "
                      "light's own synth bone")

    expected_bones = {"Root", "FreeSpotLight"}
    got_bones = {b.name for b in a.bones}
    if got_bones != expected_bones:
        errors.append(f"skeleton bones should be exactly {expected_bones}, "
                      f"got {got_bones}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (FreeSpot exports without a .Target sibling bone)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

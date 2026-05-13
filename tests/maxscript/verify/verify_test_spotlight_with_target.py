"""verify_test_spotlight_with_target.py - Phase 7b.2 baseline.

Pins:
  - TargetSpot -> ExportLight type=2 (Spotlight)
  - hotspot=30deg authored becomes 30deg-in-radians on disk
    (the empirical check for the degree-vs-radian unit question)
  - falloff=45deg authored becomes pi/4 (~0.7854) on disk -- this
    is the same value vanilla EB_ICC_LANDINGPAD.ALO uses for its
    Spot* lights, so we're matching the vanilla representation
  - Two bones from this single light: "SpotMain" + "SpotMain.Target"
  - SpotMain.Target is at the FINAL target world position (10,20,0),
    not the original (0,0,0) -- proves the walker reads the
    current target TM, not the construction-time value
  - Connection: 1 entry, light at object_index=0, pointing at the
    SpotMain bone (not the Target bone)
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
        if l.name != "SpotMain":
            errors.append(f"light name should be 'SpotMain', got {l.name!r}")
        if l.type != 2:
            errors.append(f"type should be 2 (Spotlight), got {l.type}")
        # Cone: 30deg / 45deg authored in MAXScript -> 0.5236 / 0.7854 on disk.
        expected_hotspot = math.radians(30.0)
        expected_falloff = math.radians(45.0)
        if not approx(l.hotspot, expected_hotspot, tol=1e-3):
            errors.append(f"hotspot should be ~{expected_hotspot:.4f}rad (30deg), "
                          f"got {l.hotspot:.4f} (= {math.degrees(l.hotspot):.2f}deg). "
                          f"If got 30.0, IGameProperty returned degrees and walker "
                          f"needs DegToRad conversion.")
        if not approx(l.falloff, expected_falloff, tol=1e-3):
            errors.append(f"falloff should be ~{expected_falloff:.4f}rad (45deg), "
                          f"got {l.falloff:.4f} (= {math.degrees(l.falloff):.2f}deg)")
        if l.hotspot > l.falloff + 1e-6:
            errors.append(f"hotspot ({l.hotspot}) > falloff ({l.falloff}) violates "
                          f"Max's hotspot<=falloff invariant")

    # Spotlight's own bone at the light's world position.
    sb = a.bone_by_name("SpotMain")
    if sb is None:
        errors.append("'SpotMain' bone missing")
    else:
        tx, ty, tz = sb.translation
        if not (approx(tx, 0, tol=0.01) and approx(ty, 0, tol=0.01)
                and approx(tz, 100, tol=0.01)):
            errors.append(f"SpotMain bone translation should be (0,0,100), "
                          f"got ({tx:.2f}, {ty:.2f}, {tz:.2f})")

    # Target bone at the FINAL target position (10, 20, 0).
    tb = a.bone_by_name("SpotMain.Target")
    if tb is None:
        errors.append("'SpotMain.Target' bone missing -- walker should emit a "
                      "sibling target bone for any TargetSpot with a non-null "
                      "INode::GetTarget()")
    else:
        if tb.parent_index != 0:
            errors.append(f"target bone parent should be 0 (Root), "
                          f"got {tb.parent_index}")
        tx, ty, tz = tb.translation
        if not (approx(tx, 10, tol=0.01) and approx(ty, 20, tol=0.01)
                and approx(tz, 0, tol=0.01)):
            errors.append(f"SpotMain.Target translation should be (10,20,0) "
                          f"(the FINAL target position after the post-creation "
                          f"reposition), got ({tx:.2f}, {ty:.2f}, {tz:.2f}). "
                          f"If it got (0,0,0) the walker read the construction-time "
                          f"target TM rather than the current one.")

    # Connection: light points at its own bone (not the .Target).
    if len(a.connections) != 1:
        errors.append(f"expected 1 connection, got {len(a.connections)}")
    elif sb is not None:
        spot_bone_idx = a.bones.index(sb)
        if a.connections[0].bone_index != spot_bone_idx:
            errors.append(f"light connection should point at SpotMain's bone "
                          f"(index {spot_bone_idx}), got bone#"
                          f"{a.connections[0].bone_index}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (TargetSpot + .Target bone pair; cone in radians; "
          "target world TM read at export time)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

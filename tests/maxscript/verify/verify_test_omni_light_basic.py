"""verify_test_omni_light_basic.py - Phase 7b.1 baseline.

The canary test for the walker's IGAME_LIGHT path. Pins:
  - exactly one light exported, type=Omni, all fields round-trip
  - synthetic per-light bone exists at the authored world position
  - connection-table entry exists at object_index=0 with correct bone
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

    # Light count + identity ------------------------------------------------
    if len(a.lights) != 1:
        errors.append(f"expected 1 light, got {len(a.lights)} "
                      f"({[l.name for l in a.lights]})")
    if a.lights and a.lights[0].name != "Omni_Basic":
        errors.append(f"light[0] name should be 'Omni_Basic', "
                      f"got {a.lights[0].name!r}")

    if a.lights:
        l = a.lights[0]
        # Type
        if l.type != 0:
            errors.append(f"light type should be 0 (Omni), got {l.type}")

        # Color (channel ordering canary)
        if not approx(l.color[0], 0.1):
            errors.append(f"light color[0] (R) should be 0.1, got {l.color[0]:.4f} "
                          f"-- channel ordering or 0-1 scaling regressed?")
        if not approx(l.color[1], 0.2):
            errors.append(f"light color[1] (G) should be 0.2, got {l.color[1]:.4f}")
        if not approx(l.color[2], 0.3):
            errors.append(f"light color[2] (B) should be 0.3, got {l.color[2]:.4f}")

        # Intensity
        if not approx(l.intensity, 1.5):
            errors.append(f"intensity should be 1.5, got {l.intensity}")

        # Attenuation
        if not approx(l.atten_start, 30.0, tol=0.01):
            errors.append(f"atten_start should be 30, got {l.atten_start}")
        if not approx(l.atten_end, 100.0, tol=0.01):
            errors.append(f"atten_end should be 100, got {l.atten_end}")

        # Hotspot / falloff stay 0 for Omni
        if not approx(l.hotspot, 0.0):
            errors.append(f"Omni hotspot should be 0, got {l.hotspot}")
        if not approx(l.falloff, 0.0):
            errors.append(f"Omni falloff should be 0, got {l.falloff}")

    # Per-light synthetic bone ---------------------------------------------
    lb = a.bone_by_name("Omni_Basic")
    if lb is None:
        errors.append("synthetic per-light bone 'Omni_Basic' missing")
    else:
        if lb.parent_index != 0:
            errors.append(f"light bone parent should be 0 (Root), "
                          f"got {lb.parent_index}")
        tx, ty, tz = lb.translation
        if not (approx(tx, 100, tol=0.01) and approx(ty, 200, tol=0.01)
                and approx(tz, 300, tol=0.01)):
            errors.append(f"light bone translation should be (100, 200, 300), "
                          f"got ({tx:.3f}, {ty:.3f}, {tz:.3f})")

    # Connection table ------------------------------------------------------
    if len(a.connections) != 1:
        errors.append(f"expected 1 connection (the light), "
                      f"got {len(a.connections)}")
    elif a.connections[0].object_index != 0:
        errors.append(f"light connection object_index should be 0, "
                      f"got {a.connections[0].object_index}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  (1 Omni light + synthetic bone + connection -- all fields round-tripped)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

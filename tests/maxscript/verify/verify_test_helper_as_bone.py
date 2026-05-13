"""verify_test_helper_as_bone.py - Phase 5e regression.

Pins:
  - ExportedDummy (Dummy helper, Alamo_Export_Transform=true) appears
    in the skeleton as a bone with parent_index=0 (Root) and a
    matrix whose translation column matches its Max-side position.
  - IgnoredDummy (Dummy helper, no Alamo_* prop) is *absent* from
    the skeleton -- helpers stay scene-only unless they opt in.
  - AnchorBox (plain static mesh) still gets its synthetic per-mesh
    attachment bone, proving the helper pass doesn't disturb the
    Phase 4c mesh path.
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

    # ExportedDummy must appear in the skeleton ----------------------------
    ed = a.bone_by_name("ExportedDummy")
    if ed is None:
        errors.append("'ExportedDummy' missing from skeleton -- the IGAME_HELPER "
                      "+ Alamo_Export_Transform path didn't fire")
    else:
        if ed.parent_index != 0:
            errors.append(f"ExportedDummy: parent_index should be 0 (Root), "
                          f"got {ed.parent_index}")
        tx, ty, tz = ed.translation
        if not (approx(tx, 15.0) and approx(ty, 25.0) and approx(tz, 35.0)):
            errors.append(f"ExportedDummy translation should be (15, 25, 35), "
                          f"got ({tx:.3f}, {ty:.3f}, {tz:.3f})")

    # IgnoredDummy must NOT appear -----------------------------------------
    if a.bone_by_name("IgnoredDummy") is not None:
        errors.append("'IgnoredDummy' present in skeleton but no Alamo_* prop "
                      "set -- helpers without Alamo_Export_Transform should be "
                      "ignored")

    # AnchorBox stays a mesh with its per-mesh bone -------------------------
    if a.mesh_by_name("AnchorBox") is None:
        errors.append("'AnchorBox' missing from export -- the helper pass "
                      "shouldn't disturb the mesh-walk path")
    if a.bone_by_name("AnchorBox") is None:
        errors.append("AnchorBox's synthetic per-mesh attachment bone missing")

    # Total skeleton shape: Root + ExportedDummy + AnchorBox per-mesh bone
    expected_bones = {"Root", "ExportedDummy", "AnchorBox"}
    got_bones = {b.name for b in a.bones}
    if got_bones != expected_bones:
        errors.append(f"skeleton bone set mismatch: expected {expected_bones}, "
                      f"got {got_bones}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  (ExportedDummy promoted to bone at (15, 25, 35); "
          f"IgnoredDummy omitted; AnchorBox mesh path unaffected)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

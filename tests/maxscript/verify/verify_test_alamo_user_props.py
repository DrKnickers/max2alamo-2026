"""verify_test_alamo_user_props.py - Phase 5d regression.

Pins each Alamo_* user property's round-trip through the walker:
  - PlainBox       -> baseline defaults (no flags set, billboard 0)
  - HiddenColl     -> is_hidden=1 + is_collision=1 in 0x402
  - BillboardFace  -> bone billboard_mode=2 in 0x205/0x206
  - SkippedMesh    -> entirely absent (Alamo_Export_Geometry=false)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # SkippedMesh must be absent --------------------------------------------
    if a.mesh_by_name("SkippedMesh") is not None:
        errors.append("'SkippedMesh' appears in the export but should have been "
                      "skipped (Alamo_Export_Geometry=false)")
    if a.bone_by_name("SkippedMesh") is not None:
        errors.append("'SkippedMesh' has a synthetic per-mesh bone in the export "
                      "but should have been skipped entirely")

    # The other three must be present ---------------------------------------
    expected_meshes = ["PlainBox", "HiddenColl", "BillboardFace"]
    for name in expected_meshes:
        if a.mesh_by_name(name) is None:
            errors.append(f"expected mesh {name!r} missing from export")

    # PlainBox: all flags at default -----------------------------------------
    plain = a.mesh_by_name("PlainBox")
    if plain is not None:
        if plain.is_hidden:
            errors.append("PlainBox: is_hidden should be false (no Alamo_* props set)")
        if plain.is_collision:
            errors.append("PlainBox: is_collision should be false (no Alamo_* props set)")
    plain_bone = a.bone_by_name("PlainBox")
    if plain_bone is not None and plain_bone.billboard_mode != 0:
        errors.append(f"PlainBox bone: billboard_mode should be 0 (default), "
                      f"got {plain_bone.billboard_mode}")

    # HiddenColl: both flags should be set ----------------------------------
    hc = a.mesh_by_name("HiddenColl")
    if hc is not None:
        if not hc.is_hidden:
            errors.append("HiddenColl: is_hidden should be true (Alamo_Geometry_Hidden=true)")
        if not hc.is_collision:
            errors.append("HiddenColl: is_collision should be true "
                          "(Alamo_Collision_Enabled=true)")

    # BillboardFace: synthetic bone carries billboard_mode=2 ---------------
    bf_bone = a.bone_by_name("BillboardFace")
    if bf_bone is None:
        errors.append("BillboardFace bone missing from skeleton")
    elif bf_bone.billboard_mode != 2:
        errors.append(f"BillboardFace bone: billboard_mode should be 2 (Face), "
                      f"got {bf_bone.billboard_mode}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  ({len(expected_meshes)} meshes exported, SkippedMesh omitted, "
          f"hidden/collision/billboard flags all round-tripped)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

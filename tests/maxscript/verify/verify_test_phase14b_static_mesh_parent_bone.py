"""verify_test_phase14b_static_mesh_parent_bone.py

Pins the static-mesh-inherits-from-Max-parent-bone fix. A Box parented
to a BoneSys 'HandBone' in Max must emit a synthetic per-mesh
attachment bone whose parent_index is the HandBone's index in
scene.bones, and whose matrix is the parent-LOCAL TM (not world).
Regression target: EI_SNOWTROOPER's gun used to float free of the
hand during animation playback.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # Expected skeleton: Root, HandBone, GunMesh. (3 bones total.)
    names = [b.name for b in a.bones]
    if names != ["Root", "HandBone", "GunMesh"]:
        errors.append(f"expected bones ['Root','HandBone','GunMesh'], got {names}")

    if len(a.bones) >= 3:
        hand = a.bones[1]
        gun  = a.bones[2]

        # HandBone is a real Max bone, parented to Root. Its bind
        # position is the world (500, 0, 0) the fixture authors -- if
        # this regresses to (0, -500, 0), the fixture's keyframe-orbit
        # bug has come back (see Phase 14d / #87).
        if hand.parent_index != 0:
            errors.append(f"HandBone.parent_index should be 0 (Root), "
                          f"got {hand.parent_index}")
        hx, hy, hz = hand.translation
        if abs(hx - 500.0) > 1e-2 or abs(hy) > 1e-2 or abs(hz) > 1e-2:
            errors.append(f"HandBone.translation should be (500, 0, 0), "
                          f"got ({hx:.3f}, {hy:.3f}, {hz:.3f})  "
                          f"<- if you see (0, -500, 0), the fixture's "
                          f"createBone direction baked +90 Z into the bone's "
                          f"node TM and the time-0 rotation keyframe orbited "
                          f"the position. See Phase 14d findings (#87).")

        # GunMesh is the SYNTHETIC per-mesh bone. The fix: its parent
        # should be HandBone (index 1), NOT Root (index 0).
        if gun.parent_index != 1:
            errors.append(f"GunMesh.parent_index should be 1 (HandBone), "
                          f"got {gun.parent_index}  "
                          f"<- this is the Phase 14b regression marker")

        # GunMesh.matrix translation row should be the BOX'S POSITION
        # IN HANDBONE-LOCAL SPACE. With HandBone at identity rotation +
        # T(500, 0, 0) and GunMesh world position (500, 200, 0), the
        # parent-local offset is (0, 200, 0) -- pure translation, no
        # rotation transformation involved.
        tx, ty, tz = gun.translation
        if abs(tx) > 1e-2 or abs(ty - 200.0) > 1e-2 or abs(tz) > 1e-2:
            errors.append(f"GunMesh.translation should be (0, 200, 0) in "
                          f"HandBone-local space, got ({tx:.3f}, {ty:.3f}, {tz:.3f})  "
                          f"<- if you see world coords like (500, 200, 0) "
                          f"the fix is encoding world instead of parent-local")

    # The 0x602 connection for the mesh should reference the GunMesh
    # synthetic bone (its own attachment slot), not the HandBone.
    # This invariant is independent of the parent-chain fix.
    if len(a.connections) != 1:
        errors.append(f"expected 1 connection, got {len(a.connections)}")
    elif len(a.bones) >= 3:
        c = a.connections[0]
        if c.bone_index != 2:
            errors.append(f"connection.bone_index should be 2 (GunMesh "
                          f"synth bone), got {c.bone_index}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  ({len(a.bones)} bones, GunMesh.parent={a.bones[2].parent_index}, "
          f"translation={a.bones[2].translation})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

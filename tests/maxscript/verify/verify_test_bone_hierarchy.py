"""verify_test_bone_hierarchy.py — Phase 5a regression.

Pins the real-Max-bone walk:
  - 3-bone chain B_Root -> B_Mid -> B_Tip is emitted in canonical order
    (parent before child) with correct parent_index links.
  - Each child bone's 0x206 matrix translation is the LOCAL offset from
    its parent (25 along Y for this chain), NOT the world position.
  - Synthetic Root sentinel (index 0) and per-mesh attachment bone for
    BoxA (index 4) still coexist with the real bones (Phase 4c behavior
    preserved for unskinned meshes)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def approx(a, b, tol=1e-3):
    return abs(a - b) < tol


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # Expected layout:
    #   bones[0] = Root (synthetic, parent=0xFFFFFFFF)
    #   bones[1] = B_Root (real, parent=0)
    #   bones[2] = B_Mid  (real, parent=1)
    #   bones[3] = B_Tip  (real, parent=2)
    #   bones[4] = BoxA   (synthetic per-mesh, parent=0)

    if len(a.bones) != 5:
        errors.append(f"expected 5 bones (Root + 3 real + 1 per-mesh), "
                      f"got {len(a.bones)}: {[b.name for b in a.bones]}")

    if len(a.bones) >= 1:
        if a.bones[0].name != "Root" or not a.bones[0].is_root:
            errors.append(f"bones[0] should be synthetic Root sentinel, "
                          f"got name={a.bones[0].name!r} parent={a.bones[0].parent_index}")

    # Max bone convention: each bone's length axis is its LOCAL +X. The
    # MAXScript builds the chain along world +Y, so each bone's local
    # frame is rotated 90 degrees -- world +Y maps to bone-local +X. A
    # child bone parented 25 world-units further along the chain ends up
    # with local translation ~ (25, 0, 0) (offset along its parent's
    # bone-length axis), NOT (0, 25, 0) in world coordinates.
    #
    # The discriminating test: if the bone matrix were emitted in WORLD
    # space (the pre-Phase-5a bug), B_Tip's translation would have
    # magnitude 50 (world position), not 25 (local-to-parent offset).
    expected_chain = [
        # (name, parent_index, expected_local_translation_magnitude)
        ("B_Root", 0, None),   # top-level: local == world; orientation makes
                               # individual axes hard to pin, skip
        ("B_Mid",  1, 25.0),   # local offset = 25 (one bone-length)
        ("B_Tip",  2, 25.0),   # local offset = 25 (one bone-length)
    ]
    for i, (name, parent, want_local_mag) in enumerate(expected_chain):
        bone_index = i + 1  # offset for synthetic Root at index 0
        if bone_index >= len(a.bones):
            errors.append(f"missing bones[{bone_index}] for {name}")
            continue
        b = a.bones[bone_index]
        if b.name != name:
            errors.append(f"bones[{bone_index}] should be {name!r}, got {b.name!r}")
        if b.parent_index != parent:
            errors.append(f"{name}: parent_index should be {parent}, got {b.parent_index}")
        if want_local_mag is not None:
            tx, ty, tz = b.translation
            mag = (tx * tx + ty * ty + tz * tz) ** 0.5
            if not approx(mag, want_local_mag, tol=0.1):
                errors.append(f"{name}: local translation magnitude should be "
                              f"{want_local_mag}, got {mag:.3f} ({b.translation})")
                # If the bone matrix is in WORLD space, B_Tip's magnitude
                # would be 50 (world position) rather than 25 (local offset).
                if name == "B_Tip" and approx(mag, 50.0, tol=0.1):
                    errors.append("  ^ this magnitude matches WORLD-space; "
                                  "the bone walk must use GetLocalTM, not GetWorldTM")

    # Per-mesh attachment bone for BoxA must still exist and be parented to Root.
    box_bone = a.bone_by_name("BoxA")
    if box_bone is None:
        errors.append("missing per-mesh attachment bone 'BoxA' "
                      "(static-mesh Phase 4c convention broken)")
    else:
        if box_bone.parent_index != 0:
            errors.append(f"BoxA bone parent should be Root (0), "
                          f"got {box_bone.parent_index}")
        # BoxA was placed at world (20, 0, 0) -> its synthetic per-mesh bone
        # carries the WorldTM. No Max parenting, so local == world here.
        tx, ty, tz = box_bone.translation
        if not (approx(tx, 20.0) and approx(ty, 0.0) and approx(tz, 0.0)):
            errors.append(f"BoxA bone translation should be (20, 0, 0) "
                          f"(WorldTM bake from Phase 4c), got ({tx}, {ty}, {tz})")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  ({len(a.bones)} bones, chain parent links + local matrices verified)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

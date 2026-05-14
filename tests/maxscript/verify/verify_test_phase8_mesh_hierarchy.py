"""verify_test_phase8_mesh_hierarchy.py - Phase 8f: pins local-only visibility.

The corpus-evidence-locked design (see scripts/survey_bone_visibility.py)
says: vanilla EaW/FoC content uses per-node-explicit visibility, NOT
ancestor inheritance. Smoking gun: `EB_AATURRET.ALO` bone[11] `girder`
HIDDEN and its child bone[12] `p_girder_sparks00` ALSO HIDDEN -- artist
marked the child explicitly even though parent was already hidden.

The walker honors the SDK's `INode::IsNodeHidden()` PER NODE with no
ancestor walking. This test pins that behavior so a future "let's
inherit at export" change is caught loudly.

Scene authors:
  BoxParent (Box, isHidden=true)
  BoxMid    (Box, child of BoxParent, not hidden)
  BoxLeaf   (Box, child of BoxMid, not hidden)
  LinkedBone (Bone, child of BoxMid - bone parented to mesh)

Expected on disk:
  BoxParent  bone.visible = false  (Max isHidden honored)
  BoxMid     bone.visible = true   (LOCAL-ONLY, parent's hidden does
                                    NOT propagate)
  BoxLeaf    bone.visible = true   (same)
  LinkedBone bone.visible = true   (same)

If a future walker change adds ancestor-visibility propagation, BoxMid
and BoxLeaf will turn `false` on disk and this test fires loudly.
"""
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _alo


def main(alo_path):
    errors = []
    try:
        alo = _alo.load(alo_path)
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1

    bones_by_name = {b.name: (i, b) for i, b in enumerate(alo.bones)}

    # #A1 expected bones present
    for nm in ["Root", "BoxParent", "BoxMid", "BoxLeaf", "LinkedBone"]:
        if nm not in bones_by_name:
            errors.append(f"#A1 bone {nm!r} missing")

    # #A2 BoxParent's bone.visible == false (its Max isHidden=true)
    if "BoxParent" in bones_by_name:
        _, b = bones_by_name["BoxParent"]
        if b.visible:
            errors.append(f"#A2 BoxParent.visible = true, expected false "
                          f"(isHidden=true in Max should map to visible=false on disk)")

    # #B3 BoxMid's bone.visible == true (LOCAL-ONLY: parent's hidden
    # state does NOT propagate at export). Smoking-gun assertion.
    if "BoxMid" in bones_by_name:
        _, b = bones_by_name["BoxMid"]
        if not b.visible:
            errors.append(f"#B3 BoxMid.visible = false, expected true "
                          f"(LOCAL-ONLY convention violated: parent BoxParent "
                          f"is hidden but child BoxMid should stay visible on "
                          f"disk -- the walker's INode::IsNodeHidden() reads "
                          f"per-node, not ancestor-walking)")

    # #B4 BoxLeaf's bone.visible == true (grandparent hidden, BoxLeaf
    # itself NOT hidden -> still visible per local-only)
    if "BoxLeaf" in bones_by_name:
        _, b = bones_by_name["BoxLeaf"]
        if not b.visible:
            errors.append(f"#B4 BoxLeaf.visible = false, expected true "
                          f"(grandparent hidden but BoxLeaf is local-only)")

    # #B5 LinkedBone (bone parented to mesh) gets the local-only
    # treatment too
    if "LinkedBone" in bones_by_name:
        _, b = bones_by_name["LinkedBone"]
        if not b.visible:
            errors.append(f"#B5 LinkedBone.visible = false, expected true")

    # NOTE: Phase 4c convention is static-mesh synth bones get
    # parent_index=0 (Root). The walker does NOT preserve Max-scene-graph
    # parent relationships for static meshes. (Skinned meshes connect to
    # Root too; helpers-as-bones DO honor scene-graph parenting via
    # walk_bones.) This test's value is the LOCAL-ONLY visibility
    # assertion above (#B3/B4) -- the mesh-as-parent hierarchy question
    # is documented but not asserted here.

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  ({len(alo.bones)} bones; BoxParent=hidden, BoxMid/Leaf/"
          f"LinkedBone=visible (local-only convention); 8/8 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

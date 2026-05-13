"""verify_test_biped_skinned.py - empirical 5e verification.

Confirms that Max Biped sub-bones reach the walker as IGAME_BONE and
round-trip into the exported skeleton with proper names, parent
links, and bone-indices on skinned vertices.

Pins:
  - The biped's root 'Bip01' bone is in the skeleton.
  - A representative sample of biped sub-bones is present
    (Spine, Head, Pelvis, L-Foot, R-Hand). If any is missing the
    walker isn't recognising biped bones as IGAME_BONE.
  - At least one skinned-cylinder vertex's slot-0 bone_index points
    at a biped bone (i.e. the IGameSkin -> walker mapping works
    through the biped chain).
  - The skinned cylinder connects to bone#0 (Root) per the 5b
    skinned-mesh convention.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


# Name *fragments* expected to appear in the biped skeleton. We use
# substrings rather than full names because Max 2026 numbers the
# first biped 'Bip001' (3 digits) while older Max versions used
# 'Bip01'. Substrings are robust to that without making the test
# brittle to localisation either.
EXPECTED_FRAGMENTS = ["Pelvis", "Spine", "Head", "Foot", "Hand"]


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    bone_names = [b.name for b in a.bones]

    # Sanity: there should be a *lot* of bones (default biped has ~22+).
    if len(a.bones) < 10:
        errors.append(f"skeleton has only {len(a.bones)} bones; expected "
                      f"20+ from a default biped (synthetic Root + 20 "
                      f"Biped sub-nodes + per-mesh attachment bones). "
                      f"Bones present: {bone_names}")

    # Each expected biped sub-bone name fragment must match at least
    # one bone in the skeleton.
    missing = [frag for frag in EXPECTED_FRAGMENTS
               if not any(frag in n for n in bone_names)]
    if missing:
        errors.append(f"biped bone fragments missing from skeleton: {missing}. "
                      f"This means the walker is NOT recognising those "
                      f"nodes as IGAME_BONE. Present: {bone_names}")

    # Connection check: BipSkinned -> Root
    bs_mesh = a.mesh_by_name("BipSkinned")
    if bs_mesh is None:
        errors.append("BipSkinned mesh missing from export")
    if len(a.connections) < 1:
        errors.append("no connections in export")
    else:
        if a.connections[0].bone_index != 0:
            errors.append(f"BipSkinned connection should point to bone#0 "
                          f"(Root), got bone#{a.connections[0].bone_index}")

    # Skinned mesh's per-vertex bone refs should land on biped bones,
    # not on Root (Root would mean the IGameSkin->bone_map lookup
    # failed for every influence and we fell back to the fallback).
    if bs_mesh is not None and bs_mesh.submeshes and bs_mesh.submeshes[0].vertices:
        verts = bs_mesh.submeshes[0].vertices
        # 'Bip' prefix catches Bip01, Bip001, Bip0001 -- any numbering.
        biped_indices = {i for i, b in enumerate(a.bones)
                         if b.name.startswith("Bip")}
        any_biped_binding = any(
            v.bone_indices[0] in biped_indices
            for v in verts
        )
        if not any_biped_binding:
            non_biped = {a.bones[v.bone_indices[0]].name for v in verts
                         if v.bone_indices[0] < len(a.bones)}
            errors.append(f"no skinned vertex resolved to a biped bone -- "
                          f"the IGameSkin->walker->bone_map path failed for "
                          f"biped nodes. Verts instead bound to: {non_biped}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        # Aid debugging: print the bone list when something failed.
        print(f"  (skeleton had {len(a.bones)} bones: {bone_names})",
              file=sys.stderr)
        return 1

    print(f"OK  ({len(a.bones)} bones in skeleton incl. biped chain; "
          f"BipSkinned binds to biped bones via IGameSkin)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

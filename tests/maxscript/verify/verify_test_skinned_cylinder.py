"""verify_test_skinned_cylinder.py - Phase 5b regression.

Pins:
  - Real bone hierarchy emits as Root + 3 chain bones; no synthetic
    per-mesh attachment bone gets appended for the skinned cylinder.
  - The cylinder's connection (0x602) points at bone#0 (Root), matching
    vanilla skinned content (AI_DACTILLION.ALO's "object#0 -> bone#0").
  - Per-vertex bone_indices[0] resolves to one of the chain bones
    {1, 2, 3}, never 0 (Root) -- meaning every vertex was successfully
    mapped through the IGameSkin -> walker -> writer pipeline.
  - Distribution: at least one vertex binds to each of B0, B1, B2.
    Confirms the dominant-bone resolver actually inspects per-vertex
    weights (not e.g. always returning the same bone).
  - Slot 0 has weight 1.0; slots 1..3 unused (Phase 5b is single-bone
    rigid attachment; multi-bone weighted is Phase 5c)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def approx(a, b, tol=1e-3):
    return abs(a - b) < tol


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # Bones: Root + B0 + B1 + B2 = 4 total. NO synthetic SkinnedCyl bone.
    if len(a.bones) != 4:
        errors.append(f"expected 4 bones (Root + B0 + B1 + B2), got {len(a.bones)}: "
                      f"{[b.name for b in a.bones]}")

    expected_names = ["Root", "B0", "B1", "B2"]
    for i, name in enumerate(expected_names):
        if i >= len(a.bones):
            continue
        if a.bones[i].name != name:
            errors.append(f"bones[{i}] should be {name!r}, got {a.bones[i].name!r}")
    # Crucial: ensure no per-mesh attachment bone slipped in.
    if a.bone_by_name("SkinnedCyl") is not None:
        errors.append("found synthetic 'SkinnedCyl' per-mesh attachment bone -- "
                      "skinned meshes should connect to Root, not get a per-mesh bone")

    # Connection -> Root
    if len(a.connections) != 1:
        errors.append(f"expected 1 connection, got {len(a.connections)}")
    elif a.connections[0].bone_index != 0:
        errors.append(f"skinned mesh connection should point to bone#0 (Root), "
                      f"got bone#{a.connections[0].bone_index}")

    # Per-vertex skinning checks.
    if not a.meshes or not a.meshes[0].submeshes:
        errors.append("no mesh / submesh found")
    else:
        verts = a.meshes[0].submeshes[0].vertices
        if not verts:
            errors.append("no vertices in submesh")
        else:
            bone_set = set()
            for i, v in enumerate(verts):
                # weight[0] must be 1.0; remaining weights 0 (Phase 5b single-bone)
                if not approx(v.weights[0], 1.0):
                    errors.append(f"vert[{i}]: weights[0] should be 1.0, "
                                  f"got {v.weights[0]}")
                    break
                for slot in (1, 2, 3):
                    if not approx(v.weights[slot], 0.0):
                        errors.append(f"vert[{i}]: weights[{slot}] should be 0 "
                                      f"(Phase 5b is single-bone), got {v.weights[slot]}")
                        break
                bidx0 = v.bone_indices[0]
                if bidx0 == 0:
                    errors.append(f"vert[{i}]: bone_indices[0] is 0 (Root) -- "
                                  f"a skinned vertex should resolve to one of the "
                                  f"chain bones B0/B1/B2 (indices 1/2/3)")
                    break
                if bidx0 not in (1, 2, 3):
                    errors.append(f"vert[{i}]: bone_indices[0]={bidx0} out of "
                                  f"expected range (1/2/3 for the chain bones)")
                    break
                bone_set.add(bidx0)

            # Distribution: each chain bone should claim at least 1 vertex.
            # The cylinder spans Y=0..60 with bone seams at Y=20, 40, so all
            # three buckets should be non-empty.
            for bone_idx, name in [(1, "B0"), (2, "B1"), (3, "B2")]:
                if bone_idx not in bone_set:
                    errors.append(f"no vertices bound to {name} (bone#{bone_idx}); "
                                  f"distribution check failed -- the skin resolver "
                                  f"may not be reading per-vertex weights correctly")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    verts_per_bone = {1: 0, 2: 0, 3: 0}
    for v in a.meshes[0].submeshes[0].vertices:
        verts_per_bone[v.bone_indices[0]] = verts_per_bone.get(v.bone_indices[0], 0) + 1
    print(f"OK  (skinned, connects to Root, {len(a.meshes[0].submeshes[0].vertices)} "
          f"verts split B0/B1/B2 = "
          f"{verts_per_bone[1]}/{verts_per_bone[2]}/{verts_per_bone[3]})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

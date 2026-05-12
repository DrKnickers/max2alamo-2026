"""verify_test_smooth_skinned_joint.py - Phase 5c regression.

Pins:
  - Skinned-mesh layout still holds (Root + 2 chain bones, no synthetic
    per-mesh bone, connection -> Root).
  - Multi-bone influence: at least one vertex has BOTH weights[0] > 0
    AND weights[1] > 0. The single-bone Phase 5b path would have left
    every weights[1..3] at 0; if this regresses we go right back there.
  - Every vertex's weights sum to ~1.0 (renormalization invariant).
  - Joint-plane vertices (Z ~= 40 in world space; equal to object-space
    Z because the cylinder is built along Max's native height axis with
    no rotation) carry equal 0.5/0.5 weights across (B0, B1).
  - Tip vertices (Z ~= 0) bind rigidly to B0; tip vertices (Z ~= 80)
    bind rigidly to B1 -- one influence in slot 0 with weight 1.0,
    matching the rigid-attachment serialization path.
  - Tie-break determinism: 50/50 joint verts always pack as
    bone_indices[0]=B0 (lower index), bone_indices[1]=B1.
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

    # Skeleton shape -----------------------------------------------------
    if len(a.bones) != 3:
        errors.append(f"expected 3 bones (Root + B0 + B1), got {len(a.bones)}: "
                      f"{[b.name for b in a.bones]}")
    for i, name in enumerate(["Root", "B0", "B1"]):
        if i < len(a.bones) and a.bones[i].name != name:
            errors.append(f"bones[{i}] should be {name!r}, got {a.bones[i].name!r}")
    if a.bone_by_name("SmoothCyl") is not None:
        errors.append("found synthetic 'SmoothCyl' per-mesh bone -- skinned meshes "
                      "should connect to Root, not get a per-mesh bone")

    # Connection -> Root --------------------------------------------------
    if len(a.connections) != 1:
        errors.append(f"expected 1 connection, got {len(a.connections)}")
    elif a.connections[0].bone_index != 0:
        errors.append(f"skinned mesh connection should point to bone#0 (Root), "
                      f"got bone#{a.connections[0].bone_index}")

    if not a.meshes or not a.meshes[0].submeshes:
        errors.append("no mesh / submesh found")
        _print_and_exit(errors)
        return 1

    verts = a.meshes[0].submeshes[0].vertices
    if not verts:
        errors.append("no vertices in submesh")
        _print_and_exit(errors)
        return 1

    B0 = 1
    B1 = 2

    # Per-vertex invariants ----------------------------------------------
    any_multi_bone = False
    n_joint = 0
    n_pure_b0 = 0
    n_pure_b1 = 0
    for i, v in enumerate(verts):
        wsum = sum(v.weights)
        if not approx(wsum, 1.0, 1e-3):
            errors.append(f"vert[{i}]: weights sum to {wsum:.6f}, expected 1.0 "
                          f"(weights={v.weights}, bones={v.bone_indices})")
            break

        nonzero_slots = [s for s in range(4) if v.weights[s] > 1e-6]
        if len(nonzero_slots) >= 2:
            any_multi_bone = True

        z = v.position[2]
        if approx(z, 40.0, 0.1):
            # Joint plane: expect 0.5/0.5 with deterministic tie-break ordering.
            if not (approx(v.weights[0], 0.5) and approx(v.weights[1], 0.5)):
                errors.append(f"joint vert[{i}] at Z=40: expected weights "
                              f"(0.5, 0.5, 0, 0), got {v.weights}")
                break
            if v.bone_indices[0] != B0 or v.bone_indices[1] != B1:
                errors.append(f"joint vert[{i}] at Z=40: tie-break ordering broke; "
                              f"expected bone_indices=(B0=1, B1=2, ...), "
                              f"got {v.bone_indices}")
                break
            n_joint += 1
        elif z < 20.0 + 0.1:
            # Pure-B0 region: rigid attachment, slot 0 only.
            if not (approx(v.weights[0], 1.0) and v.bone_indices[0] == B0):
                errors.append(f"pure-B0 vert[{i}] at Z={z:.2f}: expected "
                              f"(B0=1.0, ...) got weights={v.weights} "
                              f"bones={v.bone_indices}")
                break
            n_pure_b0 += 1
        elif z > 60.0 - 0.1:
            # Pure-B1 region: rigid attachment, slot 0 only.
            if not (approx(v.weights[0], 1.0) and v.bone_indices[0] == B1):
                errors.append(f"pure-B1 vert[{i}] at Z={z:.2f}: expected "
                              f"(B1=1.0, ...) got weights={v.weights} "
                              f"bones={v.bone_indices}")
                break
            n_pure_b1 += 1
        else:
            # Transition band: both bones should be present with non-zero weights.
            slot_bones = {v.bone_indices[s] for s in nonzero_slots}
            if slot_bones != {B0, B1}:
                errors.append(f"transition vert[{i}] at Z={z:.2f}: expected "
                              f"influences from both B0 and B1, got bones "
                              f"{v.bone_indices} with weights {v.weights}")
                break

    if not any_multi_bone:
        errors.append("no vertex carried multi-bone influence (all weights[1..3] "
                      "were zero) -- Phase 5c regressed to single-bone behaviour")
    if n_joint == 0:
        errors.append("no joint-plane vertex found at Z=40; the test scene must "
                      "have at least one vertex ring exactly at the joint")
    if n_pure_b0 == 0 or n_pure_b1 == 0:
        errors.append(f"distribution incomplete: pure-B0={n_pure_b0}, "
                      f"pure-B1={n_pure_b1} (each should be > 0)")

    if errors:
        _print_and_exit(errors)
        return 1

    print(f"OK  (skinned, {len(verts)} verts; joint50={n_joint}, "
          f"pureB0={n_pure_b0}, pureB1={n_pure_b1}, multi-bone present)")
    return 0


def _print_and_exit(errors):
    print("FAIL:", file=sys.stderr)
    for e in errors:
        print(f"  - {e}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

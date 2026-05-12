"""verify_test_static_box.py — pins the basic static-prop export path."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # Phase 4c convention: one Root sentinel bone + one per-mesh attachment bone.
    if len(a.bones) != 2:
        errors.append(f"expected 2 bones (Root + StaticBox), got {len(a.bones)}: "
                      f"{[b.name for b in a.bones]}")

    if a.bones:
        root = a.bones[0]
        if not root.is_root:
            errors.append(f"bone[0] should be Root sentinel (parent=0xFFFFFFFF), "
                          f"got parent={root.parent_index}")
        if root.name != "Root":
            errors.append(f"bone[0] should be named 'Root', got {root.name!r}")
        if root.translation != (0.0, 0.0, 0.0):
            errors.append(f"Root translation should be (0,0,0), got {root.translation}")

    if len(a.bones) >= 2:
        attach = a.bones[1]
        if attach.name != "StaticBox":
            errors.append(f"per-mesh bone should be named 'StaticBox', got {attach.name!r}")
        if attach.parent_index != 0:
            errors.append(f"per-mesh bone parent should be Root (0), "
                          f"got {attach.parent_index}")
        # Box at world (0,0,0) → bone matrix translation should also be ~(0,0,0).
        tx, ty, tz = attach.translation
        if abs(tx) > 1e-3 or abs(ty) > 1e-3 or abs(tz) > 1e-3:
            errors.append(f"bone matrix translation should be (0,0,0) for centered "
                          f"box, got ({tx}, {ty}, {tz})")

    # Exactly one mesh, one submesh, 36 verts (12 tris × 3 unwelded corners), 12 tris.
    if len(a.meshes) != 1:
        errors.append(f"expected 1 mesh, got {len(a.meshes)}")
    else:
        m = a.meshes[0]
        if m.name != "StaticBox":
            errors.append(f"mesh should be named 'StaticBox', got {m.name!r}")
        if len(m.submeshes) != 1:
            errors.append(f"expected 1 submesh, got {len(m.submeshes)}")
        if m.submeshes:
            sm = m.submeshes[0]
            if len(sm.vertices) != 36:
                errors.append(f"expected 36 verts (12 tris × 3 unwelded corners), "
                              f"got {len(sm.vertices)}")
            if len(sm.indices) != 36:
                errors.append(f"expected 36 indices, got {len(sm.indices)}")
            # No DirectX Shader applied → Standard-material fallback writes
            # MeshAlpha.fx by convention (per Phase 4c).
            if sm.shader_name != "MeshAlpha.fx":
                errors.append(f"shader_name should be 'MeshAlpha.fx' (Standard fallback), "
                              f"got {sm.shader_name!r}")

    # Phase 4c connection scheme: object#0 attaches to bone#1 (its per-mesh bone).
    if len(a.connections) != 1:
        errors.append(f"expected 1 connection, got {len(a.connections)}")
    elif a.connections[0].bone_index != 1:
        errors.append(f"connection should point to bone#1 (StaticBox), "
                      f"got bone#{a.connections[0].bone_index}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  ({len(a.bones)} bones, "
          f"{sum(len(sm.vertices) for m in a.meshes for sm in m.submeshes)} verts)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

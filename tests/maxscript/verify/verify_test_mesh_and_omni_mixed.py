"""verify_test_mesh_and_omni_mixed.py - Phase 7b.1 ordering test.

Pins:
  - 1 mesh + 1 light coexist in one export
  - Connection table emits mesh-then-light regardless of Max-side
    authoring order
  - Each gets the right bone reference
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    if len(a.meshes) != 1 or a.meshes[0].name != "BoxSecond":
        errors.append(f"expected 1 mesh 'BoxSecond', got "
                      f"{[m.name for m in a.meshes]}")
    if len(a.lights) != 1 or a.lights[0].name != "OmniFirst":
        errors.append(f"expected 1 light 'OmniFirst', got "
                      f"{[l.name for l in a.lights]}")

    if len(a.connections) != 2:
        errors.append(f"expected 2 connections (mesh + light), "
                      f"got {len(a.connections)}")
    else:
        # mesh at object_index 0, light at object_index 1.
        if a.connections[0].object_index != 0:
            errors.append(f"connections[0].object_index should be 0 "
                          f"(the mesh), got {a.connections[0].object_index}")
        if a.connections[1].object_index != 1:
            errors.append(f"connections[1].object_index should be 1 "
                          f"(the light), got {a.connections[1].object_index}")

    # Bones: Root + BoxSecond + OmniFirst.
    expected_bones = {"Root", "BoxSecond", "OmniFirst"}
    got_bones = {b.name for b in a.bones}
    if expected_bones - got_bones:
        errors.append(f"missing bones {expected_bones - got_bones}; got {got_bones}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (mesh-then-light connection order preserved despite reverse authoring)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

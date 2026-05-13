"""verify_test_proxy_mesh_light_combined.py - Phase 7c.2 combined accounting.

Pins:
  - 1 mesh + 1 light + 2 proxies all present
  - Connection table: 2 entries (mesh + light only); proxies are NOT
    connection objects
  - object_indices monotonic 0 (mesh), 1 (light)
  - Skeleton: Root + 1 mesh-bone + 1 light-bone + 2 proxy-bones = 5
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    if len(a.meshes) != 1 or a.meshes[0].name != "Box":
        errors.append(f"expected 1 mesh 'Box', got {[m.name for m in a.meshes]}")
    if len(a.lights) != 1 or a.lights[0].name != "Omni":
        errors.append(f"expected 1 light 'Omni', got {[l.name for l in a.lights]}")
    if {p.name for p in a.proxies} != {"p_a", "p_b"}:
        errors.append(f"expected proxies {{'p_a','p_b'}}, got "
                      f"{ {p.name for p in a.proxies} }")

    # Connections: mesh at 0, light at 1; proxies don't get 0x602 entries.
    if len(a.connections) != 2:
        errors.append(f"expected 2 connections (mesh+light), got "
                      f"{len(a.connections)}. Proxies should not be in "
                      f"the 0x602 connection table.")
    else:
        for i in range(2):
            if a.connections[i].object_index != i:
                errors.append(f"connections[{i}].object_index should be {i}, "
                              f"got {a.connections[i].object_index}")

    # Skeleton: Root + Box + Omni + p_a + p_b.
    expected_bones = {"Root", "Box", "Omni", "p_a", "p_b"}
    got_bones = {b.name for b in a.bones}
    if got_bones != expected_bones:
        errors.append(f"skeleton should be {expected_bones}, got {got_bones}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (mesh+light in connections, proxies separate; 5-bone skeleton)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

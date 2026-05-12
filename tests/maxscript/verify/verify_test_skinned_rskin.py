"""verify_test_skinned_rskin.py — integration check.

Asserts that a skinned mesh wearing an RSkin shader exports correctly:
skin path + material-params path don't interfere with each other.
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

    # --- Skeleton + connection (Phase 5a/5b) ---
    if len(a.bones) != 4:
        errors.append(f"expected 4 bones (Root + B0/B1/B2), got {len(a.bones)}: "
                      f"{[b.name for b in a.bones]}")
    if a.bone_by_name("RSkinCyl") is not None:
        errors.append("found synthetic 'RSkinCyl' per-mesh bone -- "
                      "skinned mesh should not get one")
    if len(a.connections) != 1 or a.connections[0].bone_index != 0:
        errors.append(f"connection should point to Root (bone#0), got {a.connections}")

    if not a.meshes or not a.meshes[0].submeshes:
        print("FAIL: no mesh / submesh", file=sys.stderr)
        return 1
    sm = a.meshes[0].submeshes[0]

    # --- Material (Phase 6c) ---
    if sm.shader_name != "RSkinBumpColorize.fx":
        errors.append(f"shader_name should be 'RSkinBumpColorize.fx', "
                      f"got {sm.shader_name!r}")

    expected_specular     = (25 / 255, 25 / 255, 25 / 255)
    expected_diffuse      = (200 / 255, 180 / 255, 160 / 255)
    expected_emissive     = (0.0, 0.0, 0.0)
    expected_colorization = (0.0, 1.0, 0.0, 1.0)

    def check_f3(name, want_rgb):
        p = sm.find_param(name)
        if p is None or p.float4_value is None:
            errors.append(f"{name}: missing or wrong type"); return
        r, g, b, w = p.float4_value
        wr, wg, wb = want_rgb
        if not (approx(r, wr) and approx(g, wg) and approx(b, wb)):
            errors.append(f"{name} rgb: want {want_rgb}, got ({r}, {g}, {b})")
        if not approx(w, 0.0):
            errors.append(f"{name}: 4th slot should be 0 (float3 convention), got {w}")

    def check_f4(name, want):
        p = sm.find_param(name)
        if p is None or p.float4_value is None:
            errors.append(f"{name}: missing or wrong type"); return
        for axis, (got, w) in enumerate(zip(p.float4_value, want)):
            if not approx(got, w):
                errors.append(f"{name}[{axis}]: want {w}, got {got}")

    def check_f(name, want):
        p = sm.find_param(name)
        if p is None or p.float_value is None:
            errors.append(f"{name}: missing or wrong type"); return
        if not approx(p.float_value, want):
            errors.append(f"{name}: want {want}, got {p.float_value}")

    check_f3("Emissive",     expected_emissive)
    check_f3("Diffuse",      expected_diffuse)
    check_f3("Specular",     expected_specular)
    check_f("Shininess",     8.0)
    check_f4("Colorization", expected_colorization)

    # --- Skinning (Phase 5b) ---
    bone_set = set()
    for i, v in enumerate(sm.vertices):
        if not approx(v.weights[0], 1.0):
            errors.append(f"vert[{i}]: weights[0] should be 1.0, got {v.weights[0]}")
            break
        if v.bone_indices[0] not in (1, 2, 3):
            errors.append(f"vert[{i}]: bone_indices[0]={v.bone_indices[0]} "
                          f"out of expected chain range {{1,2,3}}")
            break
        bone_set.add(v.bone_indices[0])
    if bone_set != {1, 2, 3}:
        errors.append(f"distribution: expected verts bound to all of "
                      f"{{1,2,3}}, got {bone_set}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (RSkinBumpColorize + skinning, {len(sm.vertices)} verts, "
          f"5 params verified)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

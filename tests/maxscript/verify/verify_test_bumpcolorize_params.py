"""verify_test_bumpcolorize_params.py — regression for PR #20 + PR #22.

PR #20 added per-material parameter extraction from the DirectX Shader
material's ParamBlock(0). PR #22 added the float3 alpha-zero convention
to match vanilla. Together they pin: the values set in MAXScript survive
through to the .alo bytes, AND the 4th-slot zero convention is applied
for float3-declared params (Emissive/Diffuse/Specular) but not for
genuine float4 params (Colorization)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def approx(a, b, tol=1e-3):
    return abs(a - b) < tol


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    expected_specular     = (25 / 255, 25 / 255, 25 / 255)
    expected_diffuse      = (200 / 255, 180 / 255, 160 / 255)
    expected_emissive     = (0.0, 0.0, 0.0)
    expected_colorization = (0.0, 1.0, 0.0, 1.0)
    expected_shininess    = 8.0

    if not a.meshes:
        print("FAIL: no meshes", file=sys.stderr)
        return 1

    # Both meshes share the same material -- assert on both to catch
    # walker-side per-node extraction issues.
    for m in a.meshes:
        if not m.submeshes:
            errors.append(f"{m.name}: no submeshes")
            continue
        sm = m.submeshes[0]
        if sm.shader_name != "MeshBumpColorize.fx":
            errors.append(f"{m.name}: shader_name should be 'MeshBumpColorize.fx', "
                          f"got {sm.shader_name!r}")

        def check_f3(name, want_rgb):
            p = sm.find_param(name)
            if p is None:
                errors.append(f"{m.name}.{name}: missing")
                return
            if p.kind != "float4" or p.float4_value is None:
                errors.append(f"{m.name}.{name}: expected float4 chunk")
                return
            r, g, b, w = p.float4_value
            wr, wg, wb = want_rgb
            if not (approx(r, wr) and approx(g, wg) and approx(b, wb)):
                errors.append(f"{m.name}.{name}: rgb should be {want_rgb}, got ({r}, {g}, {b})")
            if not approx(w, 0.0):
                errors.append(f"{m.name}.{name}: 4th slot should be 0.0 (float3 convention), "
                              f"got {w}")

        def check_f4(name, want):
            p = sm.find_param(name)
            if p is None:
                errors.append(f"{m.name}.{name}: missing")
                return
            if p.kind != "float4" or p.float4_value is None:
                errors.append(f"{m.name}.{name}: expected float4 chunk")
                return
            for axis, (got, w) in enumerate(zip(p.float4_value, want)):
                if not approx(got, w):
                    errors.append(f"{m.name}.{name}[{axis}]: should be {w}, got {got}")

        def check_f(name, want):
            p = sm.find_param(name)
            if p is None:
                errors.append(f"{m.name}.{name}: missing")
                return
            if p.kind != "float":
                errors.append(f"{m.name}.{name}: expected float chunk")
                return
            if not approx(p.float_value, want):
                errors.append(f"{m.name}.{name}: should be {want}, got {p.float_value}")

        check_f3("Emissive",  expected_emissive)
        check_f3("Diffuse",   expected_diffuse)
        check_f3("Specular",  expected_specular)
        check_f("Shininess",  expected_shininess)
        check_f4("Colorization", expected_colorization)

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  ({len(a.meshes)} meshes x MeshBumpColorize params verified)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

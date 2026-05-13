"""verify_test_proxy_mutex_helper_as_bone.py - Phase 7c.2 mutex test.

Pins that proxy detection trumps the Phase 5e helper-as-bone path:
  - 'p_proxy' (Alamo_Proxy + Alamo_Export_Transform=true) exports
    as a PROXY, NOT a 5e exportable bone. There's still a synth
    bone (the proxy's attachment bone), but it's the
    walker_proxies-created one, not a separate one from the
    helper-as-bone path.
  - 'MyDummy' (plain Dummy + Alamo_Export_Transform=true) exports
    as a 5e bone (Phase 5e behavior preserved).
  - Skeleton: exactly Root + p_proxy (the proxy's synth bone) +
    MyDummy. No duplicate entries.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # Proxy side: exactly p_proxy.
    if [p.name for p in a.proxies] != ["p_proxy"]:
        errors.append(f"expected proxies=['p_proxy'], got "
                      f"{[p.name for p in a.proxies]}")

    # Bone side: Root + p_proxy + MyDummy. No duplicates.
    bone_names = [b.name for b in a.bones]
    if bone_names.count("p_proxy") > 1:
        errors.append(f"'p_proxy' appears {bone_names.count('p_proxy')} times "
                      f"in skeleton -- walker is double-emitting (both as a "
                      f"proxy synth bone AND as a helper-as-bone). "
                      f"Bones: {bone_names}")
    if "MyDummy" not in bone_names:
        errors.append("'MyDummy' missing from skeleton -- Phase 5e "
                      "helper-as-bone path regressed for non-proxy helpers")

    expected = {"Root", "p_proxy", "MyDummy"}
    got = set(bone_names)
    if got != expected:
        errors.append(f"skeleton should be exactly {expected}, got {got}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (Alamo_Proxy detection wins over Alamo_Export_Transform; "
          "non-proxy helpers still respect 5e)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

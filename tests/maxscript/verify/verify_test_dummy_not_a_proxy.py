"""verify_test_dummy_not_a_proxy.py - Phase 7c.2 class-detection test.

Pins that proxy detection is by Class_ID, NOT by name prefix:
  - 'p_smoke' plain Dummy does NOT export as a proxy
  - 'p_real_proxy' Alamo_Proxy DOES
  - No 'p_smoke' bone slips into the skeleton (since Dummies without
    Alamo_Export_Transform aren't bones either)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # Only one proxy: 'p_real_proxy'. NOT 'p_smoke'.
    proxy_names = [p.name for p in a.proxies]
    if proxy_names != ["p_real_proxy"]:
        errors.append(f"expected exactly ['p_real_proxy'] in proxies, got "
                      f"{proxy_names}. If 'p_smoke' is present, the walker "
                      f"is doing name-prefix detection instead of Class_ID "
                      f"matching.")

    # The plain Dummy should also NOT appear as a bone (no Alamo_Export_Transform).
    bone_names = [b.name for b in a.bones]
    if "p_smoke" in bone_names:
        errors.append("'p_smoke' bone in skeleton -- plain Dummy without "
                      "Alamo_Export_Transform shouldn't be exported as a "
                      "bone OR a proxy. The walker is misclassifying.")

    # The Alamo_Proxy SHOULD have a synth bone.
    if "p_real_proxy" not in bone_names:
        errors.append("'p_real_proxy' synth bone missing from skeleton")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (Class_ID detection: plain Dummy ignored, Alamo_Proxy exported)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

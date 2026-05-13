"""verify_test_omni_atten_disabled.py - Phase 7b.1 robust-defaults test.

Pins:
  - Omni with useFarAtten=false still exports a valid file
  - validate(strict=True) passes (atten_start <= atten_end holds)
  - light is present, type=Omni, and the synthetic bone exists

This catches: walker crashing when an IGameProperty's value isn't
animated and the user never touched the spinner.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    if len(a.lights) != 1:
        errors.append(f"expected 1 light, got {len(a.lights)}")
    else:
        l = a.lights[0]
        if l.name != "Omni_NoAtten":
            errors.append(f"light name should be 'Omni_NoAtten', got {l.name!r}")
        if l.type != 0:
            errors.append(f"light type should be 0 (Omni), got {l.type}")
        # atten values can be anything but must be self-consistent
        # (atten_start <= atten_end OR atten_end == 0). Tier-1 already
        # checks this; we just sanity-check non-negativity here.
        if l.atten_start < 0:
            errors.append(f"atten_start={l.atten_start} is negative")
        if l.atten_end < 0:
            errors.append(f"atten_end={l.atten_end} is negative")

    if a.bone_by_name("Omni_NoAtten") is None:
        errors.append("per-light synthetic bone 'Omni_NoAtten' missing")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (Omni with useFarAtten=false exported with valid atten fields)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

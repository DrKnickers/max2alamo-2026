"""verify_test_omni_hidden_in_max.py - Phase 7b.1 visibility test.

Pins:
  - A hidden Max light still exports as an ExportLight (Max-hidden is
    an authoring-time hint, not an export-time skip)
  - The synthetic per-light bone has visible=False
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    if len(a.lights) != 1:
        errors.append(f"hidden light should still export -- got {len(a.lights)} lights")
    elif a.lights[0].name != "HiddenOmni":
        errors.append(f"light name should be 'HiddenOmni', got {a.lights[0].name!r}")

    lb = a.bone_by_name("HiddenOmni")
    if lb is None:
        errors.append("per-light bone 'HiddenOmni' missing")
    elif lb.visible:
        errors.append("per-light bone visible should be False (light was hidden in Max), "
                      "got True")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (hidden light exports with visible=False on its bone)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

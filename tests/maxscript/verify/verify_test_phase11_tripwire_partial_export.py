"""verify_test_phase11_tripwire_partial_export.py - Phase 11b tripwire A+C.

Partial-export resilience: missing _Start prop and inverted range are
both skip conditions; the GOOD clip still emits.
"""
import glob
import os
import sys


def main(alo_path):
    errors = []
    base, _ = os.path.splitext(alo_path)

    # Only GOOD must emit.
    good = base + "_GOOD.ala"
    if not os.path.isfile(good):
        errors.append(f"#T1 missing GOOD clip at {good}")

    # NOSTART and INVERTED must NOT emit.
    for clip in ("NOSTART", "INVERTED"):
        sib = base + "_" + clip + ".ala"
        if os.path.isfile(sib):
            errors.append(f"#T2 malformed clip {clip} unexpectedly emitted at {sib}")

    # No bare .ala (multi-clip path is active; un-suffixed back-compat
    # must not fire).
    if os.path.isfile(base + ".ala"):
        errors.append(f"#T3 unexpected bare .ala at {base + '.ala'}")

    # No spurious siblings beyond GOOD.
    all_sibs = sorted(os.path.normpath(p) for p in glob.glob(base + "_*.ala"))
    expected = [os.path.normpath(good)]
    if all_sibs != expected:
        errors.append(f"#T4 sibling set = {all_sibs}, expected only {expected}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (partial-export survived 2 malformed clips; GOOD emitted alone)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

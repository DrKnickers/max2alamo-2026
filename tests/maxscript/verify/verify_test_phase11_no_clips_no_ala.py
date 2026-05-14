"""verify_test_phase11_no_clips_no_ala.py - Phase 11b regression guard:
no Alamo_Anim_* user props -> no .ala emitted (bare or suffixed). This
preserves the Phase 10b walker fix.
"""
import glob
import os
import sys


def main(alo_path):
    errors = []
    base, _ = os.path.splitext(alo_path)
    if os.path.isfile(base + ".ala"):
        errors.append(f"#C1 unexpected bare .ala at {base + '.ala'}")
    suffixed = glob.glob(base + "_*.ala")
    if suffixed:
        errors.append(f"#C2 unexpected suffixed siblings: {suffixed}")
    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (no .ala emitted -- Phase 10b regression guard intact)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

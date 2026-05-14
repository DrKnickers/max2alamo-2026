"""verify_test_phase12_shadow_mixed.py — Phase 12.

Realistic mixed-content scene: an open U-trough + a closed cylinder,
both with the shadow shader. Pins:
  - exactly 1 'WARNING: shadow-volume' line in .export.log
  - the warning names 'UTrough', not 'ClosedCyl'
  - the .alo exists (export not aborted)
"""
import os
import sys


def main(path: str) -> int:
    if not os.path.isfile(path):
        print(f"FAIL: export aborted -- .alo missing: {path}", file=sys.stderr)
        return 1

    log_path = path + ".export.log"
    if not os.path.isfile(log_path):
        print(f"FAIL: missing {log_path}", file=sys.stderr)
        return 1
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        log = f.read()

    warns = [ln for ln in log.splitlines() if "WARNING: shadow-volume" in ln]
    errors = []

    if len(warns) != 1:
        errors.append(f"expected exactly 1 warning, got {len(warns)}: {warns}")
    else:
        ln = warns[0]
        if "UTrough" not in ln:
            errors.append(f"warning should name UTrough, got: {ln!r}")
        if "ClosedCyl" in ln:
            errors.append(f"warning should NOT name ClosedCyl, got: {ln!r}")

    # Also assert ClosedCyl never appears in any warning line (belt-and-braces).
    cyl_warns = [ln for ln in warns if "ClosedCyl" in ln]
    if cyl_warns:
        errors.append(f"closed cylinder spuriously flagged: {cyl_warns}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        print(f"  log contents:\n{log}", file=sys.stderr)
        return 1

    print("OK  (1 warning for UTrough, none for ClosedCyl, both meshes exported)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

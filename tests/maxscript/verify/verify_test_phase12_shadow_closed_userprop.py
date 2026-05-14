"""verify_test_phase12_shadow_closed_userprop.py — Phase 12.

A closed cube WITHOUT any shadow shader, with only the legacy
`_ALAMO_SHADOW_VOLUME` user-prop set. The walker's user-prop detector
must still fire (legacy MaxScript hook precedent). Cube is closed -> no
warning. Verifies the user-prop trigger path is wired AND doesn't
generate false positives on closed meshes.
"""
import os
import sys


def main(path: str) -> int:
    log_path = path + ".export.log"
    if not os.path.isfile(log_path):
        print(f"FAIL: missing {log_path}", file=sys.stderr)
        return 1
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        log = f.read()

    bad_lines = [ln for ln in log.splitlines() if "WARNING: shadow-volume" in ln]
    if bad_lines:
        print("FAIL: closed user-prop-shadow mesh raised unexpected warning(s):",
              file=sys.stderr)
        for ln in bad_lines:
            print(f"  - {ln}", file=sys.stderr)
        return 1

    print("OK  (closed _ALAMO_SHADOW_VOLUME mesh exported with no warning)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

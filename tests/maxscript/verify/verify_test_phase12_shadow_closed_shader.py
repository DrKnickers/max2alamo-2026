"""verify_test_phase12_shadow_closed_shader.py — Phase 12.

A closed (manifold) box with `MeshShadowVolume.fx` shader override.
The walker's shadow-volume detector fires (shader-name trigger), runs
the closed-volume validator, finds zero non-manifold edges, and emits
NO warning. This pins the no-false-positive contract on closed meshes
authored against the shadow shader.
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
        print("FAIL: closed shadow-volume mesh raised unexpected warning(s):",
              file=sys.stderr)
        for ln in bad_lines:
            print(f"  - {ln}", file=sys.stderr)
        return 1

    if not os.path.isfile(path):
        print(f"FAIL: missing .alo: {path}", file=sys.stderr)
        return 1

    print("OK  (closed shadow-volume mesh exported with no warning)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

"""verify_test_phase12_shadow_open_shader.py — Phase 12.

An open box (top face deleted) with `MeshShadowVolume.fx` shader.
Expected: exactly ONE warning line in .export.log, mentioning the mesh
name and 4 non-manifold edges (the 4 edges of the missing top face).
Export itself must still succeed (warn, don't abort).
"""
import os
import re
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
        errors.append(f"expected exactly 1 shadow warning, got {len(warns)}: {warns}")

    if warns:
        ln = warns[0]
        if "OpenShadowBox" not in ln:
            errors.append(f"warning should name 'OpenShadowBox', got: {ln!r}")
        m = re.search(r"has (\d+) non-manifold edge", ln)
        if not m:
            errors.append(f"warning does not mention a non-manifold edge count: {ln!r}")
        elif int(m.group(1)) != 4:
            errors.append(f"expected 4 non-manifold edges (one per side of "
                          f"the deleted top face), got {m.group(1)}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        print(f"  log contents:\n{log}", file=sys.stderr)
        return 1

    print("OK  (open shadow mesh warned with name + edge count, export still succeeded)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

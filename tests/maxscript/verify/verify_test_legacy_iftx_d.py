"""verify_test_legacy_iftx_d.py - Phase 10b legacy compat regression."""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _alo


def main(alo_path):
    errors = []
    try:
        alo = _alo.load(alo_path)
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr); return 1

    # Audit baseline: 5 source nodes -> 6 bones, 2 meshes on export.
    if not (4 <= len(alo.bones) <= 8):
        errors.append(f"#A1 bone count = {len(alo.bones)}, expected ~6")
    if not (1 <= len(alo.meshes) <= 3):
        errors.append(f"#A2 mesh count = {len(alo.meshes)}, expected ~2")

    ala_path = alo_path[:-4] + ".ala" if alo_path.lower().endswith(".alo") else alo_path + ".ala"
    if os.path.isfile(ala_path):
        errors.append(f"#A3 unexpected .ala at {ala_path}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (legacy IFTX_D: {len(alo.bones)} bones, {len(alo.meshes)} damage chunks)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

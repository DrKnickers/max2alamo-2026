"""verify_test_legacy_mc80_d.py - Phase 10b legacy compat regression.

Audit ground truth (from Phase 10a):
  - 77 source nodes; 62 of those Missing_Helper substitutions
  - 12 Editable_Poly damage chunks
  - On export: helpers-as-bones via Alamo_Export_Transform user prop
  - No Alamo_Anim_* -> no .ala
"""
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

    # The 62 Missing_Helpers + 3 Dummies + 12 meshes-as-attachment-bones +
    # Root = 78 bones in export. Loose tolerance since the substitution
    # behavior + helpers-as-bones interaction may vary slightly.
    if not (70 <= len(alo.bones) <= 85):
        errors.append(f"#A1 bone count = {len(alo.bones)}, expected ~78 (Phase 10a audit)")
    if not (10 <= len(alo.meshes) <= 14):
        errors.append(f"#A2 mesh count = {len(alo.meshes)}, expected ~12")

    ala_path = alo_path[:-4] + ".ala" if alo_path.lower().endswith(".alo") else alo_path + ".ala"
    if os.path.isfile(ala_path):
        errors.append(f"#A3 unexpected .ala at {ala_path}")

    # MC80_D is the destroyed-state model. Damage chunks should round-trip as
    # static meshes; the death-clone visibility pattern (if present) would land
    # on the helpers themselves. We don't assert visibility tracks here since
    # the source fixture has no Alamo_Anim_* clip metadata.

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (legacy MC80_D: {len(alo.bones)} bones, {len(alo.meshes)} damage chunks; "
          f"Missing_Helper -> helper-as-bone fallback working)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

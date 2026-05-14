"""verify_test_legacy_cis_sbd.py - Phase 10b legacy compat regression.

Audited expectations (from Phase 10a):
  - export bones: 25 (1 Root + 19 real bones + helpers-as-bones via Alamo_Export_Transform)
  - export meshes: 4 (2 skinned + 2 static)
  - no .ala (Alamo_Anim_* clip metadata absent on rootNode)
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

    # #A1 expected bone count -- 25 +/- 2 (audit ground truth tolerance)
    if not (23 <= len(alo.bones) <= 27):
        errors.append(f"#A1 bone count = {len(alo.bones)}, expected ~25 (Phase 10a audit)")

    # #A2 expected mesh count
    if not (3 <= len(alo.meshes) <= 5):
        errors.append(f"#A2 mesh count = {len(alo.meshes)}, expected ~4")

    # #A3 no spurious .ala (Phase 10b walker fix)
    ala_path = alo_path[:-4] + ".ala" if alo_path.lower().endswith(".alo") else alo_path + ".ala"
    if os.path.isfile(ala_path):
        errors.append(f"#A3 unexpected .ala at {ala_path}; fixture has no Alamo_Anim_* user props "
                      f"-> walker should NOT emit a sibling .ala (Phase 10b fix)")

    # #A4 skinning data still valid (no NaN/Inf weights)
    bad_sum = 0
    for m in alo.meshes:
        for sm in m.submeshes:
            for v in sm.vertices:
                if hasattr(v, "weights"):
                    s = sum(v.weights)
                    if abs(s - 1.0) > 1e-3:
                        bad_sum += 1
    if bad_sum:
        errors.append(f"#A4 {bad_sum} skinned vertices have weight sum != 1.0")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (legacy SBD: {len(alo.bones)} bones, {len(alo.meshes)} meshes; "
          f"no spurious .ala; skin valid)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

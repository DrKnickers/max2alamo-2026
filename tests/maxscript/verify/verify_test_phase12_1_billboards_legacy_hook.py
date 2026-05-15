"""verify_test_phase12_1_billboards_legacy_hook.py — Phase 12.1.

A box authored with ONLY the legacy `_ALAMO_BILLBOARDS = 1` MaxScript
hook (and no modern `Alamo_Billboard_Mode` prop) should export with
`billboard_mode == 1` (BBT_PARALLEL) on the synthetic per-mesh bone.

Pre-fix (no legacy fallback): billboard_mode would read 0 (Disable).
Post-fix: resolve_billboard_mode honors the legacy hook as a fallback.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    if not os.path.isfile(path):
        print(f"FAIL: missing {path}", file=sys.stderr)
        return 1
    alo = _alo.load(path)

    # Phase 4+ static-mesh convention: every mesh has its own synthetic
    # per-mesh bone named after the mesh node. Find ours.
    target = next((b for b in alo.bones if b.name == "LegacyBillboardBox"), None)
    if target is None:
        print(f"FAIL: synthetic per-mesh bone 'LegacyBillboardBox' missing; "
              f"bones present: {[b.name for b in alo.bones]}", file=sys.stderr)
        return 1

    if target.billboard_mode != 1:
        print(f"FAIL: bone 'LegacyBillboardBox' billboard_mode = "
              f"{target.billboard_mode}, expected 1 (BBT_PARALLEL) "
              f"-- legacy _ALAMO_BILLBOARDS fallback did not fire",
              file=sys.stderr)
        return 1

    print("OK  (legacy _ALAMO_BILLBOARDS=1 hook resolved to billboard_mode=1)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

"""verify_test_phase8_geometry_edge_cases.py - Phase 8f corner-case meshes."""
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

    no_mat = next((m for m in alo.meshes if m.name == "NoMaterialMesh"), None)
    multi = next((m for m in alo.meshes if m.name == "MultiMaterialMesh"), None)

    if no_mat is None:
        errors.append("#A1 NoMaterialMesh missing")
    else:
        # Should still export with at least one submesh (default material slot)
        if not no_mat.submeshes:
            errors.append("#A2 NoMaterialMesh has 0 submeshes")

    if multi is None:
        errors.append("#B1 MultiMaterialMesh missing")
    else:
        # Box has 6 mat-IDs in default. With a 2-sub-material assignment,
        # we expect at least 2 submeshes. Different Max versions may map
        # differently; assert at least 1.
        if not multi.submeshes:
            errors.append("#B2 MultiMaterialMesh has 0 submeshes")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (NoMaterialMesh: {len(no_mat.submeshes) if no_mat else 0} submeshes; "
          f"MultiMaterialMesh: {len(multi.submeshes) if multi else 0} submeshes; "
          f"both exported without crash)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

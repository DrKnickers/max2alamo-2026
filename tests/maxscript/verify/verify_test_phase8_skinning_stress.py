"""verify_test_phase8_skinning_stress.py - Phase 8f 32-bone skinning."""
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

    chain_bones = [b for b in alo.bones if b.name.startswith("ChainBone")]
    if len(chain_bones) != 32:
        errors.append(f"#A1 expected 32 ChainBone bones, got {len(chain_bones)}")

    mesh = next((m for m in alo.meshes if m.name == "StressedCyl"), None)
    if mesh is None:
        errors.append("#A2 StressedCyl mesh missing")
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    bad_sum = 0
    bad_ref = 0
    n_verts = 0
    bones_referenced = set()
    for sm in mesh.submeshes:
        for v in sm.vertices:
            n_verts += 1
            s = sum(v.weights) if hasattr(v, "weights") else 1.0
            if abs(s - 1.0) > 1e-3:
                bad_sum += 1
            if hasattr(v, "bone_indices"):
                for slot, bi in enumerate(v.bone_indices):
                    if hasattr(v, "weights") and v.weights[slot] > 0:
                        bones_referenced.add(bi)
                        if bi >= len(alo.bones):
                            bad_ref += 1
    if bad_sum:
        errors.append(f"#B3 {bad_sum} vertices have weight sum != 1.0")
    if bad_ref:
        errors.append(f"#B4 {bad_ref} vertex bone refs out of range")
    if len(bones_referenced) < 10:
        errors.append(f"#B5 only {len(bones_referenced)} distinct bones referenced; "
                      f"expected many (vertices should bind to different bones along chain)")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (32-bone chain, {n_verts} verts, {len(bones_referenced)} bones referenced; "
          f"all weight sums = 1.0; assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

"""verify_test_phase8_high_vertex_mesh.py - Phase 8f stress test."""
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

    mesh = next((m for m in alo.meshes if m.name == "HighVertexSphere"), None)
    if mesh is None:
        errors.append("#A1 HighVertexSphere mesh missing")
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    total_verts = sum(len(sm.vertices) for sm in mesh.submeshes)
    if total_verts < 10000:
        errors.append(f"#A2 only {total_verts} vertices; expected >> 10000")
    # Per-submesh limit must be < 65536 (uint16 face index)
    for i, sm in enumerate(mesh.submeshes):
        if len(sm.vertices) > 65535:
            errors.append(f"#A3 submesh[{i}] has {len(sm.vertices)} vertices, > 65535 (uint16 limit)")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  ({total_verts} vertices across {len(mesh.submeshes)} submesh(es); "
          f"under uint16 ceiling; assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

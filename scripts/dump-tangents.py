#!/usr/bin/env python3
"""Sanity check: dump the first N per-vertex (pos, normal, tangent, binormal)
records from each tangent-using submesh, and report:
  - Is tangent unit-length? (|T| - 1)
  - Is binormal unit-length?
  - Is tangent perpendicular to normal? (|dot(T, N)|)
  - Does B == cross(N, T) or cross(T, N)? (sign of dot(B, cross(N, T)))
  - Are tangent/binormal-slot bytes actually carrying tangent vectors at all?

Usage: python scripts/dump-tangents.py <vanilla.alo> [...]"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location("ct", os.path.join(os.path.dirname(__file__), "compare-tangents.py"))
ct = importlib.util.module_from_spec(spec); spec.loader.exec_module(ct)


def main():
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            data = f.read()
        sms = ct.extract_submeshes(data)
        print(f"\n{os.path.basename(path)}  ({len(sms)} submeshes)")
        for sm in sms:
            if not any(sm.shader_name.startswith(p) for p in
                       ("MeshBumpColorize","RSkinBumpColorize","MeshBumpReflectColorize",
                        "RSkinBumpReflectColorize","TerrainMeshBump","Tree","Planet")):
                continue
            n = len(sm.positions)
            # Aggregate stats over all verts
            tlen_sum = blen_sum = dot_tn_sum = handedness_sum = 0.0
            tlen_min = blen_min = float("inf")
            tlen_max = blen_max = 0.0
            zero_t = zero_b = 0
            for i in range(n):
                T = sm.tangents[i]; B = sm.binormals[i]; N = sm.normals[i]
                lt = ct.vlen(T); lb = ct.vlen(B)
                if lt < 1e-4: zero_t += 1; continue
                if lb < 1e-4: zero_b += 1
                tlen_sum += lt; blen_sum += lb
                tlen_min = min(tlen_min, lt); tlen_max = max(tlen_max, lt)
                blen_min = min(blen_min, lb); blen_max = max(blen_max, lb)
                if lt > 0:
                    Tn = (T[0]/lt, T[1]/lt, T[2]/lt)
                    dot_tn_sum += abs(ct.vdot(Tn, N))
                # Sign of (B vs cross(N,T)) -- +1 if B aligns with cross(N,T), -1 if reversed
                cnt = ct.vcross(N, T)
                if ct.vlen(cnt) > 1e-6 and lb > 1e-4:
                    handedness_sum += 1.0 if ct.vdot(B, cnt) > 0 else -1.0
            good = n - zero_t
            if good == 0:
                print(f"  {sm.mesh_name[:24]:24s} {sm.shader_name:26s} ALL ZERO tangents"); continue
            print(f"  {sm.mesh_name[:24]:24s} {sm.shader_name:26s} n={n:5d}  "
                  f"zero_T={zero_t}  zero_B={zero_b}")
            print(f"      |T|: avg {tlen_sum/good:.4f}  min {tlen_min:.4f}  max {tlen_max:.4f}")
            print(f"      |B|: avg {blen_sum/good if good else 0:.4f}  min {blen_min:.4f}  max {blen_max:.4f}")
            print(f"      |dot(T_normalized, N)|: avg {dot_tn_sum/good:.4f}  (0 = perpendicular)")
            print(f"      handedness sum: {handedness_sum:+.1f} / {good}  "
                  f"(+n => B == cross(N,T), -n => B == cross(T,N))")
            # Dump first 3 vertices raw
            for i in range(min(3, n)):
                print(f"      v[{i}] P={sm.positions[i]} N={sm.normals[i]}\n"
                      f"           T={sm.tangents[i]} B={sm.binormals[i]}")


if __name__ == "__main__":
    main()

"""verify_test_proxy_basic.py - Phase 7c.2 baseline.

The canary test for the Alamo_Proxy walker path. Pins:
  - exactly one proxy exported with the expected name
  - synth bone at the authored world position
  - default flags (is_hidden=False, alt_decrease_stay_hidden=False)
  - connection-counts mini-chunks: nConnections=0, nProxies=1
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def approx(a, b, tol=1e-3):
    return abs(a - b) < tol


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    if len(a.proxies) != 1:
        errors.append(f"expected 1 proxy, got {len(a.proxies)} "
                      f"({[p.name for p in a.proxies]})")
    elif a.proxies[0].name != "p_engine_glow":
        errors.append(f"proxy[0].name should be 'p_engine_glow', got "
                      f"{a.proxies[0].name!r}")
    elif a.proxies[0].is_hidden:
        errors.append("proxy[0].is_hidden should be False (no prop set, "
                      "node not Max-hidden)")
    elif a.proxies[0].alt_decrease_stay_hidden:
        errors.append("proxy[0].alt_decrease_stay_hidden should be False "
                      "(no prop set)")

    # Synth bone at the proxy's authored position.
    pb = a.bone_by_name("p_engine_glow")
    if pb is None:
        errors.append("per-proxy synthetic bone 'p_engine_glow' missing")
    else:
        if pb.parent_index != 0:
            errors.append(f"proxy bone parent should be 0 (Root), got "
                          f"{pb.parent_index}")
        tx, ty, tz = pb.translation
        if not (approx(tx, 50, tol=0.01) and approx(ty, 100, tol=0.01)
                and approx(tz, 200, tol=0.01)):
            errors.append(f"proxy bone translation should be (50,100,200), "
                          f"got ({tx:.2f}, {ty:.2f}, {tz:.2f})")

    # No 0x602 connections (no meshes/lights).
    if len(a.connections) != 0:
        errors.append(f"expected 0 connections, got {len(a.connections)}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (1 Alamo_Proxy + synth bone + 0x603 chunk -- all round-tripped)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

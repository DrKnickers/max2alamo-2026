"""verify_test_proxy_with_flags.py - Phase 7c.2 flag round-trip.

Pins all four (hidden, alt_decrease_stay_hidden) combinations
exported through the user-prop -> ExportProxy -> 0x603 mini-chunk
pipeline.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


# (name, expected_is_hidden, expected_alt_dec_stay_hidden)
EXPECTED = [
    ("p_defaults", False, False),
    ("p_hidden",   True,  False),
    ("p_alt_dec",  False, True),
    ("p_both",     True,  True),
]


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    if len(a.proxies) != 4:
        errors.append(f"expected 4 proxies, got {len(a.proxies)} "
                      f"({[p.name for p in a.proxies]})")

    by_name = {p.name: p for p in a.proxies}
    for name, want_hidden, want_alt in EXPECTED:
        p = by_name.get(name)
        if p is None:
            errors.append(f"proxy {name!r} missing from export")
            continue
        if p.is_hidden != want_hidden:
            errors.append(f"{name}: is_hidden should be {want_hidden}, "
                          f"got {p.is_hidden}. Either the walker isn't "
                          f"reading Alamo_Geometry_Hidden, or the writer "
                          f"isn't emitting mini-chunk 7 when set.")
        if p.alt_decrease_stay_hidden != want_alt:
            errors.append(f"{name}: alt_decrease_stay_hidden should be {want_alt}, "
                          f"got {p.alt_decrease_stay_hidden}. Either the walker "
                          f"isn't reading Alamo_Alt_Decrease_Stay_Hidden, or "
                          f"the writer isn't emitting mini-chunk 8 when set.")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (4 proxies x 2 flags all round-tripped)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

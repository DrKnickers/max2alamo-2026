"""validate_alo.py - Tier 1 invariant runner.

Invoked by scripts/run-max-tests.ps1 for each test's exported .alo
(strict mode by default, since we control those exports end-to-end).
Also usable standalone for ad-hoc validation of any file:

  python tests/maxscript/verify/validate_alo.py <file.alo>
  python tests/maxscript/verify/validate_alo.py --loose <vanilla.alo>

Strict mode adds checks our walker output should satisfy but vanilla
content routinely doesn't (sub-1e-3 normal/tangent length,
perpendicularity, sum-to-1.0 weights). Loose mode is the
vanilla-respecting structural baseline -- every file in tests/corpus/
passes in that mode.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str, strict: bool) -> int:
    a = _alo.load(path)
    errors = _alo.validate(a, strict=strict)
    if errors:
        mode = "strict" if strict else "loose"
        print(f"FAIL: {len(errors)} {mode}-mode invariant violation(s) in {path}",
              file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  ({'strict' if strict else 'loose'}-mode invariants: "
          f"{len(a.bones)} bone(s) + {len(a.meshes)} mesh(es) + "
          f"{len(a.lights)} light(s) + {len(a.proxies)} proxy(ies) + "
          f"{len(a.connections)} connection(s))")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--loose", action="store_true",
                    help="Skip strict-mode checks (default for vanilla content).")
    args = ap.parse_args()
    sys.exit(main(args.path, strict=not args.loose))

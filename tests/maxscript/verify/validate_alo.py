"""validate_alo.py - Tier 1 universal-invariant runner.

Invoked by scripts/run-max-tests.ps1 after each test's specific
verifier. Loads the exported .alo and runs _alo.validate(), which
checks the structural / skinning / connection invariants every
exported file must satisfy regardless of which test produced it.

Exits 0 if every invariant holds, 1 otherwise. Lists every violation
on stderr so a single failed export reveals all its problems in one
run (rather than playing whack-a-mole one error at a time).

Standalone use:
  python tests/maxscript/verify/validate_alo.py <path/to/file.alo>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = _alo.validate(a)
    if errors:
        print(f"FAIL: {len(errors)} invariant violation(s) in {path}",
              file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (universal invariants: skeleton + {len(a.meshes)} mesh(es) "
          f"+ {len(a.connections)} connection(s) -- all checks passed)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: validate_alo.py <file.alo>", file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))

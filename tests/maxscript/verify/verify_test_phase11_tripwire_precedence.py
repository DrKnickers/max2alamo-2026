"""verify_test_phase11_tripwire_precedence.py - Phase 11b tripwire B.

When both Alamo_Anim_Clips (multi-clip) and un-suffixed
Alamo_Anim_Start/_End/_Name (single-clip back-compat) are authored,
the multi-clip path wins deterministically. Only the FOO sibling
emits; no bare <basename>.ala.
"""
import glob
import os
import struct
import sys


def _parse_chunk_tree(data):
    out = []
    cur = 0
    while cur < len(data):
        cid, sz_word = struct.unpack_from("<II", data, cur)
        size = sz_word & 0x7FFFFFFF
        is_container = bool(sz_word & 0x80000000)
        body = data[cur + 8:cur + 8 + size]
        out.append((cid, is_container, _parse_chunk_tree(body) if is_container else body))
        cur += 8 + size
    return out


def _walk_mini(payload):
    cur = 0
    while cur + 2 <= len(payload):
        cid = payload[cur]; sz = payload[cur + 1]
        yield cid, payload[cur + 2:cur + 2 + sz]
        cur += 2 + sz


def main(alo_path):
    errors = []
    base, _ = os.path.splitext(alo_path)

    bare = base + ".ala"
    if os.path.isfile(bare):
        errors.append(f"#P1 un-suffixed back-compat unexpectedly fired -> {bare}")

    foo = base + "_FOO.ala"
    if not os.path.isfile(foo):
        errors.append(f"#P2 multi-clip FOO not emitted at {foo}")

    # No spurious siblings.
    sibs = sorted(os.path.normpath(p) for p in glob.glob(base + "_*.ala"))
    expected = [os.path.normpath(foo)]
    if sibs != expected:
        errors.append(f"#P3 sibling set = {sibs}, expected only {expected}")

    # FOO's frame count = 11 (range 0..10 inclusive). If the walker had
    # used the un-suffixed range, it'd be 21 -- this disambiguates which
    # convention actually fired.
    if os.path.isfile(foo):
        with open(foo, "rb") as f:
            data = f.read()
        tree = _parse_chunk_tree(data)
        root = next((b for c, ic, b in tree if c == 0x1000 and ic), None)
        info = next((b for c, ic, b in (root or []) if c == 0x1001 and not ic), None)
        n_frames = -1
        for mid, body in _walk_mini(info or b""):
            if mid == 1: n_frames = struct.unpack_from("<I", body)[0]
        if n_frames != 11:
            errors.append(f"#P4 FOO.n_frames = {n_frames}, expected 11 "
                          f"(0..10 from FOO_Start/End, NOT 21 from un-suffixed range)")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (multi-clip path wins precedence; un-suffixed shadowed; FOO 11 frames)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

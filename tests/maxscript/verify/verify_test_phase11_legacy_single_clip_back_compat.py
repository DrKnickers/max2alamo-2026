"""verify_test_phase11_legacy_single_clip_back_compat.py - Phase 11b
back-compat guard. Scene with only the un-suffixed Alamo_Anim_*
convention must still emit a bare <basename>.ala (Phase 8b/c/d shape)
and must NOT emit any <basename>_<CLIP>.ala siblings.
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

    # B1. Bare <basename>.ala must exist (single-clip path took effect).
    bare = base + ".ala"
    if not os.path.isfile(bare):
        errors.append(f"#B1 missing bare .ala at {bare} -- back-compat broken")

    # B2. No suffixed sibling .ala files must appear (the multi-clip path
    # didn't fire because Alamo_Anim_Clips is absent).
    suffixed = [p for p in glob.glob(base + "_*.ala")]
    if suffixed:
        errors.append(f"#B2 unexpected suffixed siblings: {suffixed}")

    # B3. The bare .ala has the expected 31-frame range (0..30).
    if os.path.isfile(bare):
        with open(bare, "rb") as f:
            data = f.read()
        tree = _parse_chunk_tree(data)
        root = next((b for c, ic, b in tree if c == 0x1000 and ic), None)
        if root is None:
            errors.append("#B3 no 0x1000 root in bare .ala")
        else:
            info = next((b for c, ic, b in root if c == 0x1001 and not ic), None)
            n_frames = -1
            for mid, body in _walk_mini(info or b""):
                if mid == 1: n_frames = struct.unpack_from("<I", body)[0]
            if n_frames != 31:
                errors.append(f"#B3 n_frames = {n_frames}, expected 31 (0..30 inclusive)")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print("OK  (bare .ala emitted; no suffixed siblings; 31-frame back-compat path intact)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

"""verify_test_phase8_anim_single_frame.py - Phase 8f off-by-one catch."""
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))


def _parse_chunk_tree(data):
    out = []
    cur = 0
    while cur < len(data):
        cid, sz_word = struct.unpack_from("<II", data, cur)
        size = sz_word & 0x7FFFFFFF
        is_container = bool(sz_word & 0x80000000)
        body = data[cur + 8:cur + 8 + size]
        if is_container:
            out.append((cid, True, _parse_chunk_tree(body)))
        else:
            out.append((cid, False, body))
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
    ala_path = alo_path[:-4] + ".ala" if alo_path.lower().endswith(".alo") else alo_path + ".ala"
    if not os.path.isfile(ala_path):
        print(f"FAIL: no .ala at {ala_path}", file=sys.stderr)
        return 1
    with open(ala_path, "rb") as f:
        data = f.read()
    tree = _parse_chunk_tree(data)
    root = next((b for c, ic, b in tree if c == 0x1000 and ic), None)
    if not root:
        print("FAIL: no 0x1000", file=sys.stderr)
        return 1
    info_payload = next((b for c, ic, b in root if c == 0x1001 and not ic), None)
    n_frames = -1
    for mid, body in _walk_mini(info_payload):
        if mid == 1 and len(body) >= 4:
            n_frames = struct.unpack_from("<I", body)[0]
    if n_frames != 1:
        errors.append(f"#A1 n_frames = {n_frames}, expected 1")
    # rot+trans pools should be exactly 1 frame x (n_words) int16
    rot_pool = next((b for c, ic, b in root if c == 0x1009 and not ic), None)
    trans_pool = next((b for c, ic, b in root if c == 0x100a and not ic), None)
    if rot_pool is None or len(rot_pool) != 4 * 2:  # 4 words x 2 bytes
        errors.append(f"#A2 rot pool size = {len(rot_pool) if rot_pool else 'none'}, expected 8")
    if trans_pool is None or len(trans_pool) != 3 * 2:
        errors.append(f"#A3 trans pool size = {len(trans_pool) if trans_pool else 'none'}, expected 6")
    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (1-frame clip; pool sizes scale correctly; 3/3 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

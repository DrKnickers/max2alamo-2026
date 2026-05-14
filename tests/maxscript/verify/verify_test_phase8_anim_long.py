"""verify_test_phase8_anim_long.py - Phase 8f 300-frame long-anim test."""
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
    ala_path = alo_path[:-4] + ".ala" if alo_path.lower().endswith(".alo") else alo_path + ".ala"
    if not os.path.isfile(ala_path):
        print(f"FAIL: no .ala", file=sys.stderr); return 1
    with open(ala_path, "rb") as f:
        data = f.read()
    tree = _parse_chunk_tree(data)
    root = next((b for c, ic, b in tree if c == 0x1000 and ic), None)
    info_payload = next((b for c, ic, b in root if c == 0x1001 and not ic), None)
    n_frames = -1
    fps = 0.0
    for mid, body in _walk_mini(info_payload):
        if mid == 1: n_frames = struct.unpack_from("<I", body)[0]
        elif mid == 2: fps = struct.unpack_from("<f", body)[0]
    if n_frames != 301:
        errors.append(f"#A1 n_frames = {n_frames}, expected 301 (frames 0..300 inclusive)")
    if abs(fps - 30.0) > 0.01:
        errors.append(f"#A2 fps = {fps}, expected 30.0")
    rot_pool = next((b for c, ic, b in root if c == 0x1009 and not ic), None)
    trans_pool = next((b for c, ic, b in root if c == 0x100a and not ic), None)
    # 301 frames x 4 rot words x 2 bytes = 2408 bytes
    if rot_pool is None or len(rot_pool) != 301 * 4 * 2:
        errors.append(f"#A3 rot pool size = {len(rot_pool) if rot_pool else 'none'}, expected {301 * 4 * 2}")
    if trans_pool is None or len(trans_pool) != 301 * 3 * 2:
        errors.append(f"#A4 trans pool size = {len(trans_pool) if trans_pool else 'none'}, expected {301 * 3 * 2}")
    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (301-frame clip; pools scale linearly to 2408+1806 bytes; 4/4 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

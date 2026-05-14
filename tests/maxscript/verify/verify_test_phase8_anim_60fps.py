"""verify_test_phase8_anim_60fps.py - Phase 8f 60-fps round-trip."""
import os, struct, sys


def _parse_chunk_tree(data):
    out = []; cur = 0
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
    if n_frames != 61:
        errors.append(f"#A1 n_frames = {n_frames}, expected 61")
    if abs(fps - 60.0) > 0.01:
        errors.append(f"#A2 fps = {fps}, expected 60.0")
    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (61 frames @ 60 fps; fps round-tripped; 2/2 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

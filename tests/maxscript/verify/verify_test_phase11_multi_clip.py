"""verify_test_phase11_multi_clip.py - Phase 11b multi-clip emission.

The exporter must emit one .ala per clip with filename
<basename>_<CLIP>.ala, NOT a bare <basename>.ala. Each .ala parses
as a valid FoC chunk tree with the per-clip frame range.
"""
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


def _parse_ala(path):
    with open(path, "rb") as f:
        data = f.read()
    tree = _parse_chunk_tree(data)
    root = next((b for c, ic, b in tree if c == 0x1000 and ic), None)
    if root is None:
        return None
    info_payload = next((b for c, ic, b in root if c == 0x1001 and not ic), None)
    n_frames = -1
    fps = 0.0
    for mid, body in _walk_mini(info_payload or b""):
        if mid == 1: n_frames = struct.unpack_from("<I", body)[0]
        elif mid == 2: fps = struct.unpack_from("<f", body)[0]
    rot_pool   = next((b for c, ic, b in root if c == 0x1009 and not ic), None)
    trans_pool = next((b for c, ic, b in root if c == 0x100a and not ic), None)
    return {
        "n_frames":  n_frames,
        "fps":       fps,
        "rot_pool":  rot_pool or b"",
        "trans_pool": trans_pool or b"",
    }


# Clip name -> expected frame count = (end - start + 1).
EXPECTED = {
    "WALK":   31,  # 0..30  inclusive
    "ATTACK": 30,  # 31..60 inclusive
    "IDLE":   30,  # 61..90 inclusive
}


def main(alo_path):
    errors = []
    base, ext = os.path.splitext(alo_path)
    if ext.lower() != ".alo":
        print(f"FAIL: expected .alo, got {ext}", file=sys.stderr); return 1

    # A1. Multi-clip path must NOT emit a bare <basename>.ala -- that's
    # the single-clip path. Its presence means the walker dispatched to
    # the un-suffixed convention instead of Alamo_Anim_Clips.
    bare = base + ".ala"
    if os.path.isfile(bare):
        errors.append(f"#A1 unexpected bare .ala emitted at {bare} "
                      f"(multi-clip path should suppress the single-clip emission)")

    # A2. Every declared clip emits a sibling at <base>_<CLIP>.ala.
    found = {}
    for clip in EXPECTED:
        sib = base + "_" + clip + ".ala"
        if not os.path.isfile(sib):
            errors.append(f"#A2 missing per-clip sibling: {sib}")
        else:
            found[clip] = sib

    # A3. Each sibling parses as a valid 0x1000 chunk tree with the
    # expected per-clip frame range.
    for clip, sib in found.items():
        parsed = _parse_ala(sib)
        if parsed is None:
            errors.append(f"#A3 {clip}: no 0x1000 root container")
            continue
        want_frames = EXPECTED[clip]
        if parsed["n_frames"] != want_frames:
            errors.append(
                f"#A3 {clip}: n_frames = {parsed['n_frames']}, expected {want_frames}"
            )
        # 30 fps from the harness's setUserProp path.
        if abs(parsed["fps"] - 30.0) > 0.01:
            errors.append(f"#A3 {clip}: fps = {parsed['fps']}, expected 30.0")
        # One animatable bone -> 4 rot words * n_frames * 2 bytes.
        want_rot = want_frames * 4 * 2
        if len(parsed["rot_pool"]) != want_rot:
            errors.append(
                f"#A3 {clip}: rot pool = {len(parsed['rot_pool'])} bytes, "
                f"expected {want_rot}"
            )
        # 3 trans words * n_frames * 2 bytes (one animatable bone).
        want_trans = want_frames * 3 * 2
        if len(parsed["trans_pool"]) != want_trans:
            errors.append(
                f"#A3 {clip}: trans pool = {len(parsed['trans_pool'])} bytes, "
                f"expected {want_trans}"
            )

    # A4. Per-clip rotation pools differ -- each clip captures a distinct
    # rotation segment, so byte-identity would mean the walker re-sampled
    # the same range three times.
    if "WALK" in found and "ATTACK" in found:
        w = _parse_ala(found["WALK"])["rot_pool"]
        a = _parse_ala(found["ATTACK"])["rot_pool"]
        if w == a:
            errors.append("#A4 WALK and ATTACK rot pools are byte-identical "
                          "(walker likely sampled the same range twice)")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  (3 sibling .ala emitted, per-clip ranges 31/30/30 frames, "
          f"distinct rot pools)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

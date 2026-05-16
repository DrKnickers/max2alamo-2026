"""_ala_dump_bone.py — diagnostic: dump a single bone's per-frame
local TM from a .ala, and (optionally) print min/max delta vs an
expected constant-local hypothesis.

Phase 14d follow-up. Intent: verify that the per-frame data the
walker emits for a parent bone matches Max's view of that bone's
local TM at the corresponding Max frame, so we can rule walker
animation-track bugs in or out.

Usage:
    python _ala_dump_bone.py <file.ala> <bone_name> [frame_idx]

Reconstruction (matches tools/ala_diff/main.cpp):
    rotation:    quat = int16_pool[i*4..(i+1)*4] / 32767.0
    translation: vec  = (uint16_pool[i*3..(i+1)*3]) * scale + offset
"""
import os
import struct
import sys


def _walk_minichunks(payload):
    cur = 0
    while cur + 2 <= len(payload):
        cid, sz = payload[cur], payload[cur + 1]
        if cur + 2 + sz > len(payload):
            raise ValueError(f"truncated mini-chunk at {cur}")
        yield cid, payload[cur + 2:cur + 2 + sz]
        cur += 2 + sz


def _parse(data):
    out = []
    cur = 0
    while cur < len(data):
        if cur + 8 > len(data):
            raise ValueError(f"truncated chunk header at {cur}")
        cid, sz_word = struct.unpack_from("<II", data, cur)
        size = sz_word & 0x7FFFFFFF
        is_container = bool(sz_word & 0x80000000)
        body = data[cur + 8:cur + 8 + size]
        if is_container:
            out.append((cid, True, _parse(body)))
        else:
            out.append((cid, False, body))
        cur = cur + 8 + size
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: _ala_dump_bone.py <file.ala> <bone_name> [frame_idx]")
        return 1
    path = sys.argv[1]
    target_name = sys.argv[2]
    frame_idx = int(sys.argv[3]) if len(sys.argv) >= 4 else 0

    with open(path, "rb") as f:
        data = f.read()
    tree = _parse(data)

    if not tree or tree[0][0] != 0x1000 or not tree[0][1]:
        print(f"ERROR: top chunk not 0x1000 container")
        return 1
    top = tree[0][2]

    n_frames = 0
    n_rot_words = 0
    n_trans_words = 0
    bones = []  # list of dicts {name, idx_translation, idx_rotation, default_rotation, trans_offset, trans_scale}
    rotation_pool = b''
    translation_pool = b''

    for cid, is_container, body in top:
        if cid == 0x1001 and not is_container:
            for mid, mbody in _walk_minichunks(body):
                if mid == 1 and len(mbody) >= 4:
                    n_frames = struct.unpack_from("<I", mbody, 0)[0]
                elif mid == 11 and len(mbody) >= 4:
                    n_rot_words = struct.unpack_from("<I", mbody, 0)[0]
                elif mid == 12 and len(mbody) >= 4:
                    n_trans_words = struct.unpack_from("<I", mbody, 0)[0]
        elif cid == 0x1002 and is_container:
            bone = {"name": "", "idx_translation": -1, "idx_rotation": -1,
                    "default_rotation": (0, 0, 0, 32767),
                    "trans_offset": (0.0, 0.0, 0.0),
                    "trans_scale": (0.0, 0.0, 0.0)}
            for cid2, ic2, body2 in body:
                if cid2 == 0x1003 and not ic2:
                    for mid, mbody in _walk_minichunks(body2):
                        if mid == 4:
                            n = len(mbody)
                            while n > 0 and mbody[n - 1] == 0:
                                n -= 1
                            bone["name"] = mbody[:n].decode("utf-8", errors="replace")
                        elif mid == 6 and len(mbody) >= 12:
                            bone["trans_offset"] = struct.unpack_from("<3f", mbody, 0)
                        elif mid == 7 and len(mbody) >= 12:
                            bone["trans_scale"] = struct.unpack_from("<3f", mbody, 0)
                        elif mid == 14 and len(mbody) >= 2:
                            bone["idx_translation"] = struct.unpack_from("<h", mbody, 0)[0]
                        elif mid == 16 and len(mbody) >= 2:
                            bone["idx_rotation"] = struct.unpack_from("<h", mbody, 0)[0]
                        elif mid == 17 and len(mbody) >= 8:
                            bone["default_rotation"] = struct.unpack_from("<4h", mbody, 0)
            bones.append(bone)
        elif cid == 0x1009 and not is_container:
            rotation_pool = body
        elif cid == 0x100a and not is_container:
            translation_pool = body

    print(f"== {path} ==")
    print(f"  n_frames        = {n_frames}")
    print(f"  n_rot_words     = {n_rot_words} (=> rotation pool = {n_rot_words * n_frames * 2} bytes; have {len(rotation_pool)})")
    print(f"  n_trans_words   = {n_trans_words} (=> translation pool = {n_trans_words * n_frames * 2} bytes; have {len(translation_pool)})")
    print(f"  bones count     = {len(bones)}")

    target = None
    for b in bones:
        if b["name"] == target_name:
            target = b
            break
    if not target:
        print(f"ERROR: bone {target_name!r} not found. Available: {[b['name'] for b in bones[:10]]} ...")
        return 1

    print(f"\n--- {target_name} track metadata ---")
    print(f"  idx_translation  = {target['idx_translation']}")
    print(f"  idx_rotation     = {target['idx_rotation']}")
    print(f"  trans_offset     = {target['trans_offset']}")
    print(f"  trans_scale      = {target['trans_scale']}")
    print(f"  default_rotation = {target['default_rotation']} (raw int16) = {tuple(v / 32767.0 for v in target['default_rotation'])} (unpacked quat)")

    # Reconstruct frame frame_idx
    print(f"\n--- frame {frame_idx} (of {n_frames}) reconstruction ---")
    if target["idx_rotation"] >= 0 and n_rot_words > 0:
        offset = (frame_idx * n_rot_words + target["idx_rotation"]) * 2
        if offset + 8 <= len(rotation_pool):
            qx, qy, qz, qw = struct.unpack_from("<4h", rotation_pool, offset)
            print(f"  rotation int16   = ({qx}, {qy}, {qz}, {qw})")
            print(f"  rotation quat    = ({qx/32767:.5f}, {qy/32767:.5f}, {qz/32767:.5f}, {qw/32767:.5f})")
        else:
            print(f"  rotation: pool too short")
    else:
        print(f"  rotation: NO TRACK (idx={target['idx_rotation']}); using default = {tuple(v/32767.0 for v in target['default_rotation'])}")

    if target["idx_translation"] >= 0 and n_trans_words > 0:
        offset = (frame_idx * n_trans_words + target["idx_translation"]) * 2
        if offset + 6 <= len(translation_pool):
            ux, uy, uz = struct.unpack_from("<3H", translation_pool, offset)
            ox, oy, oz = target["trans_offset"]
            sx, sy, sz = target["trans_scale"]
            tx = ux * sx + ox
            ty = uy * sy + oy
            tz = uz * sz + oz
            print(f"  translation u16  = ({ux}, {uy}, {uz})")
            print(f"  translation real = ({tx:.5f}, {ty:.5f}, {tz:.5f})")
        else:
            print(f"  translation: pool too short")
    else:
        print(f"  translation: NO TRACK (idx={target['idx_translation']})")

    return 0


if __name__ == "__main__":
    sys.exit(main())

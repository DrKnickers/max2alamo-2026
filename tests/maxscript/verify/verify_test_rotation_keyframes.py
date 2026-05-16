"""verify_test_rotation_keyframes.py - Phase 8b rotation-pass acceptance.

Pins:
  - .ala exists alongside the .alo
  - Top-level 0x1000 + 0x1001 (info) + per-bone 0x1002 + file-scope 0x1009
  - n_frames = 31 (inclusive of both ends)
  - FoC framing (mini-chunks 11/12/13 in 0x1001)
  - n_rotation_words = 4 (one animated bone)
  - n_translation_words = 0 (8b out-of-scope)
  - 0x100a NOT present
  - TestBone is in the bone list with idx_rotation = 0
  - Other bones have idx_rotation = -1
  - Frame 0 quat ~ identity, Frame 30 quat ~ (0, 0, 0.7071, 0.7071)
  - All frames have unit-length quats (within 1e-2)
  - No frame-to-frame sign discontinuity (dot > 0)
"""
import math
import os
import struct
import sys

_PI4 = math.pi / 4.0  # 45 deg
_SIN45 = math.sin(_PI4)
_COS45 = math.cos(_PI4)

_TOL_QUAT_FRAME    = 1e-3
_TOL_UNIT_QUAT     = 1e-2  # int16 quantisation -> length within 1/32767
_TOL_ZERO_COMPONENT = 1.5e-4


def _read_u32_le(b):
    return struct.unpack_from("<I", b, 0)[0]


def _read_i16_le(b):
    return struct.unpack_from("<h", b, 0)[0]


def _read_f32_le(b):
    return struct.unpack_from("<f", b, 0)[0]


def _walk_minichunks(payload):
    """Yield (id, body_bytes) tuples for each mini-chunk in `payload`."""
    cur = 0
    while cur + 2 <= len(payload):
        cid = payload[cur]
        sz  = payload[cur + 1]
        if cur + 2 + sz > len(payload):
            raise ValueError(f"truncated mini-chunk at {cur}")
        yield cid, payload[cur + 2:cur + 2 + sz]
        cur += 2 + sz


def _parse_chunk_tree(data):
    """Return a list of (id, is_container, payload | children) recursively."""
    out = []
    cur = 0
    while cur < len(data):
        if cur + 8 > len(data):
            raise ValueError(f"truncated chunk header at {cur}")
        cid, sz_word = struct.unpack_from("<II", data, cur)
        size = sz_word & 0x7FFFFFFF
        is_container = bool(sz_word & 0x80000000)
        payload_off = cur + 8
        if payload_off + size > len(data):
            raise ValueError(f"chunk 0x{cid:X} payload runs past buffer")
        body = data[payload_off:payload_off + size]
        if is_container:
            out.append((cid, True, _parse_chunk_tree(body)))
        else:
            out.append((cid, False, body))
        cur = payload_off + size
    return out


def _find_root_1000(tree):
    for cid, is_container, body in tree:
        if cid == 0x1000 and is_container:
            return body
    return None


def _unpack_quat(int16_xyzw):
    return tuple(c / 32767.0 for c in int16_xyzw)


def main(alo_path):
    errors = []
    ala_path = alo_path[:-4] + ".ala" if alo_path.lower().endswith(".alo") else alo_path + ".ala"

    # #1 .ala exists alongside the .alo.
    if not os.path.isfile(ala_path):
        print(f"FAIL: expected .ala at {ala_path}", file=sys.stderr)
        return 1

    with open(ala_path, "rb") as f:
        data = f.read()

    try:
        tree = _parse_chunk_tree(data)
    except Exception as e:
        print(f"FAIL: chunk-tree parse: {e}", file=sys.stderr)
        return 1

    # #2 Top-level 0x1000 container.
    root_children = _find_root_1000(tree)
    if root_children is None:
        errors.append("#2 missing top-level 0x1000 container")

    # Pull out 0x1001 (info), per-bone 0x1002s, and file-scope pool leaves.
    info_payload = None
    bone_containers = []
    rot_pool_payload = None
    trans_pool_payload = None
    if root_children:
        for cid, is_container, body in root_children:
            if cid == 0x1001 and not is_container:
                info_payload = body
            elif cid == 0x1002 and is_container:
                bone_containers.append(body)
            elif cid == 0x1009 and not is_container:
                rot_pool_payload = body
            elif cid == 0x100a and not is_container:
                trans_pool_payload = body

    if info_payload is None:
        errors.append("#2 missing 0x1001 info leaf")

    # Parse 0x1001 mini-chunks.
    n_frames = -1
    fps = 0.0
    n_bones_declared = -1
    n_rotation_words = -1
    n_translation_words = -1
    n_scale_words = -1
    saw_foc_mini = False
    if info_payload is not None:
        try:
            for mid, body in _walk_minichunks(info_payload):
                if   mid ==  1 and len(body) >= 4: n_frames = _read_u32_le(body)
                elif mid ==  2 and len(body) >= 4: fps = _read_f32_le(body)
                elif mid ==  3 and len(body) >= 4: n_bones_declared = _read_u32_le(body)
                elif mid == 11 and len(body) >= 4:
                    n_rotation_words = _read_u32_le(body); saw_foc_mini = True
                elif mid == 12 and len(body) >= 4:
                    n_translation_words = _read_u32_le(body); saw_foc_mini = True
                elif mid == 13 and len(body) >= 4:
                    n_scale_words = _read_u32_le(body); saw_foc_mini = True
        except Exception as e:
            errors.append(f"#2 0x1001 mini-chunk parse: {e}")

    # #3 n_frames == 31.
    if n_frames != 31:
        errors.append(f"#3 n_frames = {n_frames}, expected 31")

    # #4 n_bones declared >= 2.
    if n_bones_declared < 2:
        errors.append(f"#4 n_bones declared = {n_bones_declared}, expected >= 2")

    # #5 FoC framing (any of mini 11/12/13 present).
    if not saw_foc_mini:
        errors.append("#5 0x1001 has no FoC mini-chunks (11/12/13) -- expected FoC format")

    # #6 n_rotation_words == 4.
    if n_rotation_words != 4:
        errors.append(f"#6 n_rotation_words = {n_rotation_words}, expected 4")

    # #7 n_translation_words == 3 (Phase 8c forward-compat: every animatable
    # bone gets a translation track regardless of whether position animates;
    # TestBone has constant position so its translation pool values are all
    # zero, but the slot is still emitted).
    if n_translation_words != 3:
        errors.append(f"#7 n_translation_words = {n_translation_words}, expected 3")

    # #8 0x1009 size == n_frames * n_rotation_words * 2.
    if rot_pool_payload is None:
        errors.append("#8 0x1009 (rotation pool) missing")
    else:
        expected_pool_bytes = (max(n_frames, 0) *
                               max(n_rotation_words, 0) * 2)
        if len(rot_pool_payload) != expected_pool_bytes:
            errors.append(f"#8 0x1009 size = {len(rot_pool_payload)}, "
                          f"expected {expected_pool_bytes} "
                          f"(n_frames {n_frames} * n_rot_words {n_rotation_words} * 2)")

    # #9 0x100a present (Phase 8c: emitted for the single animatable bone
    # with constant position; pool values are all zero but the chunk exists).
    if trans_pool_payload is None:
        errors.append("#9 0x100a (translation pool) missing; expected present after 8c")
    else:
        expected_trans_bytes = (max(n_frames, 0) *
                                max(n_translation_words, 0) * 2)
        if len(trans_pool_payload) != expected_trans_bytes:
            errors.append(f"#9 0x100a size = {len(trans_pool_payload)}, "
                          f"expected {expected_trans_bytes}")

    # Parse per-bone 0x1003 mini-chunks to get name + idx_rotation.
    bones_info = []  # list of dicts: {name, idx_rotation, idx_translation, idx_scale, default_rotation}
    bone_names = []
    for bone_body in bone_containers:
        info = {"name": "", "idx_rotation": -1, "idx_translation": -1,
                "idx_scale": -1, "default_rotation": None}
        # Find the 0x1003 leaf inside this 0x1002 container.
        for cid, is_container, body in bone_body:
            if cid == 0x1003 and not is_container:
                try:
                    for mid, mbody in _walk_minichunks(body):
                        if mid == 4:
                            n = len(mbody)
                            while n > 0 and mbody[n - 1] == 0:
                                n -= 1
                            info["name"] = mbody[:n].decode("utf-8", errors="replace")
                        elif mid == 14 and len(mbody) >= 2:
                            info["idx_translation"] = _read_i16_le(mbody)
                        elif mid == 15 and len(mbody) >= 2:
                            info["idx_scale"] = _read_i16_le(mbody)
                        elif mid == 16 and len(mbody) >= 2:
                            info["idx_rotation"] = _read_i16_le(mbody)
                        elif mid == 17 and len(mbody) >= 8:
                            info["default_rotation"] = tuple(
                                _read_i16_le(mbody[k*2:k*2+2]) for k in range(4))
                except Exception as e:
                    errors.append(f"0x1003 mini-chunk parse for bone idx {len(bones_info)}: {e}")
        bones_info.append(info)
        bone_names.append(info["name"])

    # #10 TestBone is in the bone list.
    test_bone = next((b for b in bones_info if b["name"] == "TestBone"), None)
    if test_bone is None:
        errors.append(f"#10 'TestBone' not found in {bone_names}")

    # #11 TestBone.idx_rotation == 0 (and idx_translation == 0 post-8c).
    if test_bone is not None and test_bone["idx_rotation"] != 0:
        errors.append(f"#11 TestBone.idx_rotation = {test_bone['idx_rotation']}, expected 0")
    if test_bone is not None and test_bone["idx_translation"] != 0:
        errors.append(f"#11 TestBone.idx_translation = {test_bone['idx_translation']}, "
                      f"expected 0 (Phase 8c emits translation tracks for animatable bones)")

    # #12 Other bones have idx_rotation == -1 (and idx_translation == -1).
    for b in bones_info:
        if b["name"] != "TestBone" and b["idx_rotation"] != -1:
            errors.append(f"#12 bone {b['name']!r} idx_rotation = {b['idx_rotation']}, "
                          f"expected -1")
        if b["name"] != "TestBone" and b["idx_translation"] != -1:
            errors.append(f"#12 bone {b['name']!r} idx_translation = {b['idx_translation']}, "
                          f"expected -1")

    # Unpack quats from the pool for frame 0 and frame 30 at TestBone slot 0.
    frame_quats = []
    if rot_pool_payload and n_rotation_words and n_rotation_words > 0:
        try:
            stride = n_rotation_words  # words per frame
            for f in range(max(n_frames, 0)):
                base = f * stride * 2  # bytes
                if base + 8 > len(rot_pool_payload):
                    break
                q = tuple(_read_i16_le(rot_pool_payload[base + k*2: base + k*2 + 2])
                          for k in range(4))
                frame_quats.append(_unpack_quat(q))
        except Exception as e:
            errors.append(f"pool decode: {e}")

    # #13 Frame 0 quat ~ identity (0, 0, 0, 1).
    if frame_quats:
        x, y, z, w = frame_quats[0]
        delta = max(abs(x), abs(y), abs(z), abs(w - 1.0))
        if delta > _TOL_QUAT_FRAME:
            errors.append(f"#13 frame[0] quat = ({x:.5f}, {y:.5f}, {z:.5f}, {w:.5f}), "
                          f"expected ~(0, 0, 0, 1); max-delta {delta:.5f} > {_TOL_QUAT_FRAME}")

    # #14 Frame 30 quat ~ (0, 0, -sin45, cos45).
    # Post-Phase-14a (commit bf35d5e): extract_rotation_quat now
    # conjugates the result of `Quat(Matrix3)` to undo Max's IGame
    # convention flip. Every walker-emitted rotation quat now has
    # its xyz components negated relative to the pre-14a output
    # (the rotation it represents is what the engine plays back
    # correctly -- visually verified on EI_SNOWTROOPER's 60 clips).
    # The on-disk quat for authored "+90 deg around Z" is therefore
    # (0, 0, -sin45, cos45), not (0, 0, +sin45, cos45).
    if len(frame_quats) >= 31:
        x, y, z, w = frame_quats[30]
        delta = max(abs(x), abs(y), abs(z + _SIN45), abs(w - _COS45))
        if delta > _TOL_QUAT_FRAME:
            errors.append(f"#14 frame[30] quat = ({x:.5f}, {y:.5f}, {z:.5f}, {w:.5f}), "
                          f"expected ~(0, 0, {-_SIN45:.5f}, {_COS45:.5f}); "
                          f"max-delta {delta:.5f} > {_TOL_QUAT_FRAME}")

    # #15 All frames have unit-length quaternions.
    if frame_quats:
        worst_len_err = 0.0
        for f, (x, y, z, w) in enumerate(frame_quats):
            ssq = x*x + y*y + z*z + w*w
            if abs(ssq - 1.0) > worst_len_err:
                worst_len_err = abs(ssq - 1.0)
            if abs(ssq - 1.0) > _TOL_UNIT_QUAT:
                errors.append(f"#15 frame[{f}] quat |q|^2 = {ssq:.5f}, "
                              f"deviates from 1 by {abs(ssq-1):.5f} > {_TOL_UNIT_QUAT}")
                break

    # #16 No bone name empty or has embedded nulls.
    for b in bones_info:
        if not b["name"]:
            errors.append("#16 a bone has empty name")
        if "\x00" in b["name"]:
            errors.append(f"#16 bone {b['name']!r} has embedded null")

    # #17 Sign canonicalisation: dot(q[f], q[f+1]) > 0 for all f.
    if len(frame_quats) >= 2:
        worst_dot = 1.0
        for f in range(len(frame_quats) - 1):
            a = frame_quats[f]
            c = frame_quats[f + 1]
            d = a[0]*c[0] + a[1]*c[1] + a[2]*c[2] + a[3]*c[3]
            if d < worst_dot:
                worst_dot = d
            if d <= 0.0:
                errors.append(f"#17 sign discontinuity at frame {f} -> {f+1}: "
                              f"dot = {d:.5f} (should be > 0)")
                break

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    f0_q = frame_quats[0] if frame_quats else None
    f30_q = frame_quats[30] if len(frame_quats) > 30 else None
    print(f"OK  ({n_frames} frames @ {fps:.1f} fps, "
          f"n_bones={n_bones_declared}, n_rot_words={n_rotation_words}; "
          f"frame[0]={f0_q}, frame[30]={f30_q})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

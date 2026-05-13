"""verify_test_translation_keyframes.py - Phase 8c translation-pass acceptance.

Pins (25 assertions across 4 groups):
  A. Structural — .ala exists, chunk-tree shape, pool sizes, FoC framing,
     0x100a precedes 0x1009, n_scale_words=0
  B. Slot assignment — TransBone idx_rot=0/idx_trans=0; RotBone idx_rot=4/
     idx_trans=3; Root/AnchorBox idx_*=-1; no empty names
  C. Rotation correctness — RotBone frame[0] ~ identity (or frame-stable);
     RotBone frame[30] applies 90deg Z relative to frame 0; TransBone
     rotation pool frames are mutually consistent; all quats unit-length;
     sign-canonicalisation continuous
  D. Translation correctness — TransBone offset/scale match authored
     (0,0,0) -> (10,20,5); frame[0] unpacks to (0,0,0); frame[30] unpacks
     to (10,20,5); RotBone has degenerate scale and frame-stable positions
"""
import math
import os
import struct
import sys

_PI4 = math.pi / 4.0
_SIN45 = math.sin(_PI4)
_COS45 = math.cos(_PI4)

_TOL_QUAT_FRAME = 1e-3
_TOL_UNIT_QUAT  = 1e-2
_TOL_TRANS_ABS  = 1e-3      # tolerance on decoded position in world units
_TOL_TRANS_SCALE_REL = 5e-4 # tolerance on per-axis trans_scale (relative)


def _read_u32_le(b): return struct.unpack_from("<I", b, 0)[0]
def _read_i16_le(b): return struct.unpack_from("<h", b, 0)[0]
def _read_f32_le(b): return struct.unpack_from("<f", b, 0)[0]


def _walk_minichunks(payload):
    cur = 0
    while cur + 2 <= len(payload):
        cid = payload[cur]; sz = payload[cur + 1]
        if cur + 2 + sz > len(payload):
            raise ValueError(f"truncated mini-chunk at {cur}")
        yield cid, payload[cur + 2:cur + 2 + sz]
        cur += 2 + sz


def _parse_chunk_tree(data):
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


def _unpack_quat(int16_xyzw):
    return tuple(c / 32767.0 for c in int16_xyzw)


def _unpack_position(int16_xyz, offset, scale):
    # int16 on disk is a bit-reinterpreted uint16; cast back before scaling.
    out = []
    for i in range(3):
        u = int16_xyz[i] & 0xFFFF
        out.append(u * scale[i] + offset[i])
    return tuple(out)


def main(alo_path):
    errors = []
    ala_path = alo_path[:-4] + ".ala" if alo_path.lower().endswith(".alo") else alo_path + ".ala"

    # #A1 .ala exists.
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

    # Locate 0x1000 and capture its children with their on-disk byte offsets
    # (to verify 0x100a precedes 0x1009).
    root_children = None
    for cid, is_container, body in tree:
        if cid == 0x1000 and is_container:
            root_children = body
            break
    if root_children is None:
        errors.append("#A2 missing top-level 0x1000")

    info_payload = None
    bone_containers = []
    rot_pool_payload = None
    trans_pool_payload = None
    child_order = []
    if root_children:
        for cid, is_container, body in root_children:
            child_order.append(cid)
            if cid == 0x1001 and not is_container:
                info_payload = body
            elif cid == 0x1002 and is_container:
                bone_containers.append(body)
            elif cid == 0x1009 and not is_container:
                rot_pool_payload = body
            elif cid == 0x100a and not is_container:
                trans_pool_payload = body

    # Parse 0x1001 mini-chunks.
    n_frames = -1
    fps = 0.0
    n_bones_declared = -1
    n_rotation_words = -1
    n_translation_words = -1
    n_scale_words = -1
    saw_foc_mini = False
    if info_payload is not None:
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

    # Group A — Structural
    if n_frames != 31:
        errors.append(f"#A3 n_frames = {n_frames}, expected 31")
    if n_bones_declared < 4:
        errors.append(f"#A4 n_bones declared = {n_bones_declared}, expected >= 4")
    if not saw_foc_mini:
        errors.append("#A5 0x1001 has no FoC mini-chunks (11/12/13)")
    if n_rotation_words != 8:
        errors.append(f"#A6 n_rotation_words = {n_rotation_words}, expected 8")
    if n_translation_words != 6:
        errors.append(f"#A7 n_translation_words = {n_translation_words}, expected 6")
    if n_scale_words != 0:
        errors.append(f"#A8 n_scale_words = {n_scale_words}, expected 0")
    expected_rot_bytes   = max(n_frames, 0) * max(n_rotation_words, 0)    * 2
    expected_trans_bytes = max(n_frames, 0) * max(n_translation_words, 0) * 2
    if rot_pool_payload is None:
        errors.append("#A9 0x1009 (rotation pool) missing")
    elif len(rot_pool_payload) != expected_rot_bytes:
        errors.append(f"#A9 0x1009 size = {len(rot_pool_payload)}, expected {expected_rot_bytes}")
    if trans_pool_payload is None:
        errors.append("#A10 0x100a (translation pool) missing")
    elif len(trans_pool_payload) != expected_trans_bytes:
        errors.append(f"#A10 0x100a size = {len(trans_pool_payload)}, expected {expected_trans_bytes}")
    # 0x100a must appear before 0x1009 (per vanilla corpus convention).
    try:
        i100a = child_order.index(0x100a)
        i1009 = child_order.index(0x1009)
        if i100a >= i1009:
            errors.append(f"#A11 0x100a (idx {i100a}) does not precede 0x1009 (idx {i1009})")
    except ValueError:
        errors.append("#A11 missing 0x100a or 0x1009 in chunk order")

    # Parse per-bone 0x1003 mini-chunks: name + idx_rotation + idx_translation
    # + trans_offset + trans_scale.
    bones_info = []
    for bone_body in bone_containers:
        info = {"name": "", "idx_rotation": -1, "idx_translation": -1,
                "idx_scale": -1,
                "trans_offset": [0.0, 0.0, 0.0],
                "trans_scale":  [0.0, 0.0, 0.0]}
        for cid, is_container, body in bone_body:
            if cid == 0x1003 and not is_container:
                for mid, mbody in _walk_minichunks(body):
                    if mid == 4:
                        n = len(mbody)
                        while n > 0 and mbody[n - 1] == 0: n -= 1
                        info["name"] = mbody[:n].decode("utf-8", errors="replace")
                    elif mid == 6 and len(mbody) >= 12:
                        info["trans_offset"] = [_read_f32_le(mbody[k*4:k*4+4]) for k in range(3)]
                    elif mid == 7 and len(mbody) >= 12:
                        info["trans_scale"]  = [_read_f32_le(mbody[k*4:k*4+4]) for k in range(3)]
                    elif mid == 14 and len(mbody) >= 2:
                        info["idx_translation"] = _read_i16_le(mbody)
                    elif mid == 15 and len(mbody) >= 2:
                        info["idx_scale"] = _read_i16_le(mbody)
                    elif mid == 16 and len(mbody) >= 2:
                        info["idx_rotation"] = _read_i16_le(mbody)
        bones_info.append(info)

    trans_bone = next((b for b in bones_info if b["name"] == "TransBone"), None)
    rot_bone   = next((b for b in bones_info if b["name"] == "RotBone"),   None)
    other_bones = [b for b in bones_info if b["name"] not in ("TransBone", "RotBone")]

    # Group B — Slot assignment
    if trans_bone is None:
        errors.append("#B12 TransBone missing")
    else:
        if trans_bone["idx_rotation"] != 0:
            errors.append(f"#B12 TransBone.idx_rotation = {trans_bone['idx_rotation']}, expected 0")
        if trans_bone["idx_translation"] != 0:
            errors.append(f"#B12 TransBone.idx_translation = {trans_bone['idx_translation']}, expected 0")
    if rot_bone is None:
        errors.append("#B13 RotBone missing")
    else:
        if rot_bone["idx_rotation"] != 4:
            errors.append(f"#B13 RotBone.idx_rotation = {rot_bone['idx_rotation']}, expected 4")
        if rot_bone["idx_translation"] != 3:
            errors.append(f"#B13 RotBone.idx_translation = {rot_bone['idx_translation']}, expected 3")
    for b in other_bones:
        if b["idx_rotation"] != -1:
            errors.append(f"#B14 bone {b['name']!r} idx_rotation = {b['idx_rotation']}, expected -1")
        if b["idx_translation"] != -1:
            errors.append(f"#B14 bone {b['name']!r} idx_translation = {b['idx_translation']}, expected -1")
    for b in bones_info:
        if not b["name"]: errors.append("#B15 a bone has empty name")
        if "\x00" in b["name"]: errors.append(f"#B15 bone {b['name']!r} has embedded null")

    # Decode per-frame rotation pool slots.
    def decode_quat_track(idx, stride):
        out = []
        if rot_pool_payload is None: return out
        for f in range(max(n_frames, 0)):
            base = (f * stride + idx) * 2
            if base + 8 > len(rot_pool_payload): break
            q = tuple(_read_i16_le(rot_pool_payload[base + k*2: base + k*2 + 2]) for k in range(4))
            out.append(_unpack_quat(q))
        return out

    # Decode per-frame translation pool slots.
    def decode_pos_track(idx, stride, offset, scale):
        out = []
        if trans_pool_payload is None: return out
        for f in range(max(n_frames, 0)):
            base = (f * stride + idx) * 2
            if base + 6 > len(trans_pool_payload): break
            ints = tuple(_read_i16_le(trans_pool_payload[base + k*2: base + k*2 + 2]) for k in range(3))
            out.append(_unpack_position(ints, offset, scale))
        return out

    # Group C — Rotation
    if rot_bone is not None and n_rotation_words > 0:
        rotbone_q = decode_quat_track(rot_bone["idx_rotation"], n_rotation_words)
        if rotbone_q:
            # The bone's static orientation depends on BoneSys defaults; we
            # don't assert frame[0] = identity globally. Instead: take frame[0]
            # as the reference, and assert frame[30] differs by ~90deg around Z.
            qref = rotbone_q[0]
            # Apply the inverse of qref to frame[30] and check the result ~ 90Z.
            # For unit quaternion qref^-1 = (-x, -y, -z, w).
            # Quat multiplication: q1 * q2 (Hamilton product).
            def qmul(a, b):
                return (a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1],
                        a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0],
                        a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3],
                        a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2])
            qref_inv = (-qref[0], -qref[1], -qref[2], qref[3])
            if len(rotbone_q) > 30:
                rel = qmul(qref_inv, rotbone_q[30])
                # rel should be a quaternion representing +90deg about Z:
                # (0, 0, sin45, cos45). Allow sign flip via abs(rel[3]).
                # Force canonical hemisphere with positive w.
                if rel[3] < 0: rel = tuple(-c for c in rel)
                delta = max(abs(rel[0]), abs(rel[1]),
                            abs(rel[2] - _SIN45), abs(rel[3] - _COS45))
                if delta > _TOL_QUAT_FRAME:
                    errors.append(f"#C17 RotBone frame[30] relative to frame[0] = {rel}, "
                                  f"expected (0,0,{_SIN45},{_COS45}); delta={delta}")
            # Frame[0] unit-length check (captures any pack/unpack bug).
            ssq0 = sum(c*c for c in qref)
            if abs(ssq0 - 1.0) > _TOL_UNIT_QUAT:
                errors.append(f"#C16 RotBone frame[0] |q|^2={ssq0}, expected 1.0")

    if trans_bone is not None and n_rotation_words > 0:
        transbone_q = decode_quat_track(trans_bone["idx_rotation"], n_rotation_words)
        # TransBone has no rotation animation, so all frames should be
        # nearly identical. Assert frame-to-frame stability (dot > 1 - 1e-3).
        for f in range(1, len(transbone_q)):
            a = transbone_q[f - 1]; c = transbone_q[f]
            d = a[0]*c[0] + a[1]*c[1] + a[2]*c[2] + a[3]*c[3]
            if abs(d) < 1.0 - 1e-2:
                errors.append(f"#C18 TransBone rot frame {f-1}->{f} drifted: dot={d}")
                break

    # Quat unit-length sweep + sign-canonicalisation continuity (both tracks).
    for track_name, tinfo in (("TransBone", trans_bone), ("RotBone", rot_bone)):
        if tinfo is None or tinfo["idx_rotation"] < 0: continue
        qs = decode_quat_track(tinfo["idx_rotation"], n_rotation_words)
        for f, q in enumerate(qs):
            ssq = sum(c*c for c in q)
            if abs(ssq - 1.0) > _TOL_UNIT_QUAT:
                errors.append(f"#C19 {track_name} frame[{f}] |q|^2={ssq:.5f} not unit"); break
        for f in range(len(qs) - 1):
            a = qs[f]; b = qs[f + 1]
            d = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3]
            if d <= 0:
                errors.append(f"#C19 {track_name} sign discontinuity at frame {f}->{f+1}: dot={d}")
                break

    # Group D — Translation
    if trans_bone is not None:
        expected_scale = (10.0 / 65535.0, 20.0 / 65535.0, 5.0 / 65535.0)
        # #D20 trans_offset ~ (0,0,0)
        for axis in range(3):
            if abs(trans_bone["trans_offset"][axis]) > _TOL_TRANS_ABS:
                errors.append(f"#D20 TransBone.trans_offset[{axis}] = "
                              f"{trans_bone['trans_offset'][axis]:.6f}, expected ~0")
        # #D21 trans_scale ~ expected (relative tolerance against axis magnitude)
        for axis in range(3):
            got = trans_bone["trans_scale"][axis]
            exp = expected_scale[axis]
            if abs(got - exp) > _TOL_TRANS_SCALE_REL * max(abs(exp), 1e-9):
                errors.append(f"#D21 TransBone.trans_scale[{axis}] = {got:.9f}, "
                              f"expected ~{exp:.9f}")
        if n_translation_words > 0:
            ps = decode_pos_track(trans_bone["idx_translation"], n_translation_words,
                                  trans_bone["trans_offset"], trans_bone["trans_scale"])
            # #D22 frame[0] ~ (0,0,0)
            if ps:
                p0 = ps[0]
                d = max(abs(p0[0]), abs(p0[1]), abs(p0[2]))
                if d > _TOL_TRANS_ABS:
                    errors.append(f"#D22 frame[0] position = {p0}, expected ~(0,0,0); d={d}")
            # #D23 frame[30] ~ (10,20,5)
            if len(ps) > 30:
                p30 = ps[30]
                d = max(abs(p30[0] - 10.0), abs(p30[1] - 20.0), abs(p30[2] - 5.0))
                if d > _TOL_TRANS_ABS:
                    errors.append(f"#D23 frame[30] position = {p30}, expected ~(10,20,5); d={d}")

    if rot_bone is not None:
        # #D24 RotBone has constant position -> trans_scale ~ (0,0,0)
        for axis in range(3):
            if abs(rot_bone["trans_scale"][axis]) > 1e-6:
                errors.append(f"#D24 RotBone.trans_scale[{axis}] = {rot_bone['trans_scale'][axis]:.9f}, "
                              f"expected 0 (constant position)")
        if n_translation_words > 0:
            ps = decode_pos_track(rot_bone["idx_translation"], n_translation_words,
                                  rot_bone["trans_offset"], rot_bone["trans_scale"])
            # #D25 frame[0] == frame[N] (constant track)
            if len(ps) > 30:
                d = max(abs(ps[0][k] - ps[30][k]) for k in range(3))
                if d > 1e-6:
                    errors.append(f"#D25 RotBone position drifted: frame[0]={ps[0]}, "
                                  f"frame[30]={ps[30]}; d={d}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  ({n_frames} frames @ {fps:.1f} fps, n_bones={n_bones_declared}, "
          f"rot_words={n_rotation_words}, trans_words={n_translation_words}; "
          f"25/25 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

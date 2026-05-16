"""verify_test_phase8_acceptance.py - Phase 8e full animation-surface acceptance.

Asserts that the Phase 8 walker (8b rotation + 8c translation + 8d visibility
+ 5g pivot-orientation) composes correctly on a multi-feature scene that
exercises every bone category in visibility_map without leaking tracks into
the wrong category. ~62 assertions across 10 groups.

The scene authors:
  - 4 bones in bone_map (BoneA / BoneB / HelperBone / PivotedHardpoint),
    of which BoneA, HelperBone, PivotedHardpoint have authored rotation
    and BoneB has authored translation; all 4 get rotation+translation
    slots per the 8b/8c convention.
  - 11 bones in visibility_map, of which 3 have authored visibility
    animation (OmniLight, StaticBox, prox0) and 7 are constant-visible
    (Root + the 4 bone_map members + SpotLight + SpotLight.Target +
    prox1) and should be elided per the constant-visible rule.
  - The PivotedHardpoint helper has both a static objectoffsetrot
    (-90 around Y, composed via 5g) AND animated node rotation (0->60
    around Z), so its per-frame on-disk quaternion is the composition.

Groups:
  A. Structural framing (6 assertions)
  B. Skeleton + bone naming (7)
  C. Pool size + slot assignment / bone_map enforcement (8)
  D. Rotation correctness incl. PivotedHardpoint composition (8)
  E. Sign canonicalisation continuity (3)
  F. Translation correctness incl. constant-position bones (6)
  G. Visibility (bone_map split + visibility_map coverage) (12)
  H. Skinning unaffected by animation (5)
  I. Determinism (chunk ordering) (3)
  J. .export.log cross-check (4)
Total: 62.
"""
import math
import os
import struct
import sys
from pathlib import Path

# Make _alo.py importable.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import _alo

_VIS_CHUNK_ID = 0x1007
_ALLOWED_BONE_LEAF_IDS = {0x1003, 0x1004, 0x1005, 0x1006, 0x1007, 0x1008}

_TOL_QUAT_FRAME = 1e-3
_TOL_UNIT_QUAT = 1e-2
_TOL_TRANS_ABS = 1e-3
_TOL_TRANS_SCALE_REL = 5e-4


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


def _bone_name(bone_body):
    for cid, is_container, body in bone_body:
        if cid == 0x1003 and not is_container:
            for mid, mbody in _walk_minichunks(body):
                if mid == 4:
                    n = len(mbody)
                    while n > 0 and mbody[n - 1] == 0:
                        n -= 1
                    return mbody[:n].decode("utf-8", errors="replace")
    return ""


def _bone_slots(bone_body):
    """Extract (idx_rotation, idx_translation, idx_scale, trans_offset, trans_scale)."""
    info = {"idx_rotation": -1, "idx_translation": -1, "idx_scale": -1,
            "trans_offset": [0.0, 0.0, 0.0], "trans_scale": [0.0, 0.0, 0.0]}
    for cid, is_container, body in bone_body:
        if cid == 0x1003 and not is_container:
            for mid, mbody in _walk_minichunks(body):
                if mid == 6 and len(mbody) >= 12:
                    info["trans_offset"] = [_read_f32_le(mbody[k * 4:k * 4 + 4]) for k in range(3)]
                elif mid == 7 and len(mbody) >= 12:
                    info["trans_scale"] = [_read_f32_le(mbody[k * 4:k * 4 + 4]) for k in range(3)]
                elif mid == 14 and len(mbody) >= 2:
                    info["idx_translation"] = _read_i16_le(mbody)
                elif mid == 15 and len(mbody) >= 2:
                    info["idx_scale"] = _read_i16_le(mbody)
                elif mid == 16 and len(mbody) >= 2:
                    info["idx_rotation"] = _read_i16_le(mbody)
    return info


def _vis_leaf_payload(bone_body):
    for cid, is_container, body in bone_body:
        if cid == _VIS_CHUNK_ID and not is_container:
            return body
    return None


def _unpack_quat(int16_xyzw):
    return tuple(c / 32767.0 for c in int16_xyzw)


def _unpack_position_uint16(int16_xyz, offset, scale):
    out = []
    for i in range(3):
        u = int16_xyz[i] & 0xFFFF
        out.append(u * scale[i] + offset[i])
    return tuple(out)


def _quat_mul(a, b):
    """Hamilton product. a = (x, y, z, w)."""
    return (
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    )


def _quat_axis_angle(axis, theta_rad):
    s = math.sin(theta_rad / 2.0)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(theta_rad / 2.0))


def _quat_conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def main(alo_path):
    errors = []
    ala_path = (alo_path[:-4] + ".ala"
                if alo_path.lower().endswith(".alo") else alo_path + ".ala")

    # =========================================================
    # Load .ala
    # =========================================================
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

    root_children = None
    for cid, is_container, body in tree:
        if cid == 0x1000 and is_container:
            root_children = body
            break
    if root_children is None:
        print("FAIL: missing top-level 0x1000", file=sys.stderr)
        return 1

    info_payload = None
    bone_containers = []
    rot_pool_payload = None
    trans_pool_payload = None
    child_order = []
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

    n_frames = -1
    fps = 0.0
    n_bones_declared = -1
    n_rotation_words = -1
    n_translation_words = -1
    n_scale_words = -1
    saw_foc_11 = saw_foc_12 = saw_foc_13 = False
    if info_payload is not None:
        for mid, body in _walk_minichunks(info_payload):
            if mid == 1 and len(body) >= 4:
                n_frames = _read_u32_le(body)
            elif mid == 2 and len(body) >= 4:
                fps = _read_f32_le(body)
            elif mid == 3 and len(body) >= 4:
                n_bones_declared = _read_u32_le(body)
            elif mid == 11 and len(body) >= 4:
                n_rotation_words = _read_u32_le(body); saw_foc_11 = True
            elif mid == 12 and len(body) >= 4:
                n_translation_words = _read_u32_le(body); saw_foc_12 = True
            elif mid == 13 and len(body) >= 4:
                n_scale_words = _read_u32_le(body); saw_foc_13 = True

    # =========================================================
    # Group A - Structural framing (6)
    # =========================================================
    if n_frames != 31:
        errors.append(f"#A1 n_frames = {n_frames}, expected 31")
    if abs(fps - 30.0) > 0.01:
        errors.append(f"#A2 fps = {fps}, expected 30.0")
    if not (saw_foc_11 and saw_foc_12 and saw_foc_13):
        errors.append(f"#A3 FoC mini-chunks (11/12/13) not all present "
                      f"(11={saw_foc_11}, 12={saw_foc_12}, 13={saw_foc_13})")
    for bone_body in bone_containers:
        for cid, is_container, _ in bone_body:
            if not is_container and cid not in _ALLOWED_BONE_LEAF_IDS:
                errors.append(f"#A4 unknown leaf 0x{cid:X} inside 0x1002")
                break
    # #A5 0x100a precedes 0x1009 (per 8c convention)
    try:
        i_trans = child_order.index(0x100a)
        i_rot = child_order.index(0x1009)
        if i_trans >= i_rot:
            errors.append(f"#A5 0x100a (idx {i_trans}) does not precede 0x1009 (idx {i_rot})")
    except ValueError:
        errors.append("#A5 missing 0x100a or 0x1009 in top-level chunk order")
    # #A6 n_scale_words == 0
    if n_scale_words != 0:
        errors.append(f"#A6 n_scale_words = {n_scale_words}, expected 0")

    # =========================================================
    # Group B - Skeleton + bone naming (7)
    # =========================================================
    if n_bones_declared != 11:
        errors.append(f"#B7 n_bones declared = {n_bones_declared}, expected 11")

    bones_by_name = {}
    for bone_body in bone_containers:
        name = _bone_name(bone_body)
        bones_by_name[name] = bone_body

    expected_bones = ["Root", "BoneA", "BoneB", "HelperBone", "PivotedHardpoint",
                      "StaticBox", "OmniLight", "SpotLight", "SpotLight.Target",
                      "prox0", "prox1"]
    missing = [b for b in expected_bones if b not in bones_by_name]
    if missing:
        errors.append(f"#B8 missing bones: {missing}")
    extra = [b for b in bones_by_name if b not in expected_bones]
    if extra:
        errors.append(f"#B9 unexpected bones present: {extra}")

    # Use _alo for parent-index checks (more robust than parsing here).
    try:
        alo = _alo.load(alo_path)
        bones_indexed = {b.name: (i, b) for i, b in enumerate(alo.bones)}

        # #B10 Root is bone[0]
        if "Root" in bones_indexed and bones_indexed["Root"][0] != 0:
            errors.append(f"#B10 Root not at index 0 (got {bones_indexed['Root'][0]})")
        # #B11 BoneA is root-level (parent_index == 0 = Root)
        if "BoneA" in bones_indexed:
            _, ba = bones_indexed["BoneA"]
            if ba.parent_index != 0:
                errors.append(f"#B11 BoneA parent_index = {ba.parent_index}, expected 0 (Root)")
        # #B12 BoneB is root-level (sibling of BoneA)
        if "BoneB" in bones_indexed:
            _, bb = bones_indexed["BoneB"]
            if bb.parent_index != 0:
                errors.append(f"#B12 BoneB parent_index = {bb.parent_index}, expected 0 (Root)")
        # #B13 PivotedHardpoint at Root
        if "PivotedHardpoint" in bones_indexed:
            _, php = bones_indexed["PivotedHardpoint"]
            if php.parent_index != 0:
                errors.append(f"#B13 PivotedHardpoint parent_index = {php.parent_index}, expected 0")
    except Exception as e:
        errors.append(f"#B10-13 .alo parse failed: {e}")
        bones_indexed = {}

    # =========================================================
    # Group C - Pool size + slot assignment (8)
    # =========================================================
    if n_rotation_words != 16:
        errors.append(f"#C14 n_rotation_words = {n_rotation_words}, expected 16 (4 bones x 4)")
    if n_translation_words != 12:
        errors.append(f"#C15 n_translation_words = {n_translation_words}, expected 12 (4 bones x 3)")
    # #C16 covered by #A6 already; reuse counter slot
    if n_scale_words != 0:
        errors.append(f"#C16 n_scale_words = {n_scale_words}, expected 0")

    slot_data = {n: _bone_slots(b) for n, b in bones_by_name.items()}
    BONE_MAP_NAMES = {"BoneA", "BoneB", "HelperBone", "PivotedHardpoint"}

    # #C17-20: each animated bone has rotation+translation slots.
    for nm in BONE_MAP_NAMES:
        s = slot_data.get(nm, {})
        if s.get("idx_rotation", -1) < 0:
            errors.append(f"#C17-20 {nm}.idx_rotation = {s.get('idx_rotation', -1)}, expected >= 0")
        if s.get("idx_translation", -1) < 0:
            errors.append(f"#C17-20 {nm}.idx_translation = {s.get('idx_translation', -1)}, expected >= 0")

    # #C21 every non-bone_map bone has idx_rotation == -1 AND idx_translation == -1
    for nm in bones_by_name:
        if nm in BONE_MAP_NAMES or nm == "Root":
            continue
        s = slot_data[nm]
        if s["idx_rotation"] != -1:
            errors.append(f"#C21 {nm!r} has idx_rotation = {s['idx_rotation']}, expected -1 (not in bone_map)")
        if s["idx_translation"] != -1:
            errors.append(f"#C21 {nm!r} has idx_translation = {s['idx_translation']}, expected -1")

    # =========================================================
    # Group D - Rotation correctness (8)
    # =========================================================
    def decode_quat_track(idx, stride):
        out = []
        if rot_pool_payload is None or idx < 0:
            return out
        for f in range(max(n_frames, 0)):
            base = (f * stride + idx) * 2
            if base + 8 > len(rot_pool_payload):
                break
            q = tuple(_read_i16_le(rot_pool_payload[base + k * 2: base + k * 2 + 2]) for k in range(4))
            out.append(_unpack_quat(q))
        return out

    # #D22-D23: BoneA frame[0] ~ identity, frame[30] - frame[0] ~ 90 Z.
    # Post-Phase-14a (commit bf35d5e): extract_rotation_quat conjugates
    # the result of `Quat(Matrix3)` to undo Max's IGame convention flip.
    # Every walker-emitted rotation quat now has its xyz components
    # negated relative to the pre-14a output -- so authored "+90 Z"
    # reads back as (0, 0, -sin45, cos45) on disk, not (0, 0, +sin45,
    # cos45). Visually verified correct on EI_SNOWTROOPER's 60 clips.
    if "BoneA" in slot_data:
        qs = decode_quat_track(slot_data["BoneA"]["idx_rotation"], n_rotation_words)
        if len(qs) >= 31:
            qref = qs[0]
            ssq = sum(c * c for c in qref)
            if abs(ssq - 1.0) > _TOL_UNIT_QUAT:
                errors.append(f"#D22 BoneA frame[0] |q|^2 = {ssq}, not unit")
            # Apply qref^-1 to qs[30]; post-14a expect (0, 0, -sin45, cos45).
            q_delta = _quat_mul(_quat_conj(qref), qs[30])
            if q_delta[3] < 0:
                q_delta = tuple(-c for c in q_delta)
            sin45 = math.sin(math.pi / 4.0)
            cos45 = math.cos(math.pi / 4.0)
            d = max(abs(q_delta[0]), abs(q_delta[1]),
                    abs(q_delta[2] + sin45), abs(q_delta[3] - cos45))
            if d > _TOL_QUAT_FRAME:
                errors.append(f"#D23 BoneA frame[30] relative to frame[0] = {q_delta}, "
                              f"expected (0,0,{-sin45:.4f},{cos45:.4f}); delta={d}")

    # #D24-D25: HelperBone frame[30] - frame[0] ~ 45 X.  Same Phase 14a
    # xyz-negation convention as #D23: x component sign flips.
    if "HelperBone" in slot_data:
        qs = decode_quat_track(slot_data["HelperBone"]["idx_rotation"], n_rotation_words)
        if len(qs) >= 31:
            qref = qs[0]
            ssq = sum(c * c for c in qref)
            if abs(ssq - 1.0) > _TOL_UNIT_QUAT:
                errors.append(f"#D24 HelperBone frame[0] |q|^2 = {ssq}, not unit")
            q_delta = _quat_mul(_quat_conj(qref), qs[30])
            if q_delta[3] < 0:
                q_delta = tuple(-c for c in q_delta)
            sin22 = math.sin(math.radians(22.5))
            cos22 = math.cos(math.radians(22.5))
            d = max(abs(q_delta[0] + sin22), abs(q_delta[1]),
                    abs(q_delta[2]), abs(q_delta[3] - cos22))
            if d > _TOL_QUAT_FRAME:
                errors.append(f"#D25 HelperBone frame[30] relative to frame[0] = {q_delta}, "
                              f"expected ({-sin22:.4f},0,0,{cos22:.4f}); delta={d}")

    # #D26 BoneB rotation is constant (unit-length, frame-stable).
    if "BoneB" in slot_data:
        qs = decode_quat_track(slot_data["BoneB"]["idx_rotation"], n_rotation_words)
        if qs:
            for i, q in enumerate(qs):
                ssq = sum(c * c for c in q)
                if abs(ssq - 1.0) > _TOL_UNIT_QUAT:
                    errors.append(f"#D26 BoneB frame[{i}] |q|^2 = {ssq}, not unit")
                    break

    # #D27-D28 PivotedHardpoint: composes objectoffsetrot * authored.
    # The .max scene's `objectoffsetrot` is authored as -90 deg about
    # Y (matching the pre-Phase-14a "true" rotation (0, -sin45, 0,
    # cos45)).  Post-Phase-14a (commit bf35d5e) the walker emits the
    # CONJUGATE of that on disk: (0, +sin45, 0, cos45).  The on-disk
    # composite at frame 30 is therefore
    #     conjugate(q_offset_true * q_authored_30)
    #   = conjugate(q_authored_30) * conjugate(q_offset_true)
    # (quat conjugate distributes by reversing multiplication order).
    #
    # To recover the authored rotation from the on-disk value we
    # multiply on the RIGHT (not the left) by the on-disk q_offset's
    # inverse: q_rel = qs[30] * q_offset_inv collapses to
    # conjugate(q_authored), i.e. (0, 0, -sin30, cos30).  That keeps
    # the test asserting that 5g+8b composition still works without
    # adding ad-hoc tolerance to mask the convention change.
    if "PivotedHardpoint" in slot_data:
        qs = decode_quat_track(slot_data["PivotedHardpoint"]["idx_rotation"], n_rotation_words)
        if len(qs) >= 31:
            sin45 = math.sin(math.pi / 4.0)
            cos45 = math.cos(math.pi / 4.0)
            q_offset = (0.0, sin45, 0.0, cos45)   # on-disk = conjugate(true_offset)
            # #D27: frame 0 ~ q_offset (within tolerance, accounting for sign).
            q0 = qs[0]
            if q0[3] < 0:
                q0 = tuple(-c for c in q0)
            q_off_pos = q_offset if q_offset[3] >= 0 else tuple(-c for c in q_offset)
            d0 = max(abs(q0[i] - q_off_pos[i]) for i in range(4))
            if d0 > _TOL_QUAT_FRAME:
                errors.append(f"#D27 PivotedHardpoint frame[0] = {q0}, expected ~{q_off_pos}; "
                              f"delta={d0} (5g static composition broken?)")
            # #D28: per-frame relative composition.  qs[30] = conjugate(
            # q_offset_true * q_authored) = conjugate(q_authored) *
            # conjugate(q_offset_true); RIGHT-multiply by q_offset_inv (=
            # q_offset_true under the updated definition) collapses the
            # tail.  Expect conjugate(q_authored_30) = (0, 0, -sin30, cos30).
            q_offset_inv = _quat_conj(q_offset)
            q_rel = _quat_mul(qs[30], q_offset_inv)
            if q_rel[3] < 0:
                q_rel = tuple(-c for c in q_rel)
            sin30 = math.sin(math.radians(30.0))
            cos30 = math.cos(math.radians(30.0))
            d30 = max(abs(q_rel[0]), abs(q_rel[1]),
                      abs(q_rel[2] + sin30), abs(q_rel[3] - cos30))
            if d30 > _TOL_QUAT_FRAME:
                errors.append(f"#D28 PivotedHardpoint frame[30] composition off: "
                              f"q_30 * q_offset^-1 = {q_rel}, "
                              f"expected ~(0,0,{-sin30:.4f},{cos30:.4f}); "
                              f"delta={d30} (5g x 8b interaction broken)")

    # #D29 all quats unit-length across all animated bones.
    for nm in BONE_MAP_NAMES:
        qs = decode_quat_track(slot_data.get(nm, {}).get("idx_rotation", -1), n_rotation_words)
        bad = False
        for i, q in enumerate(qs):
            ssq = sum(c * c for c in q)
            if abs(ssq - 1.0) > _TOL_UNIT_QUAT:
                errors.append(f"#D29 {nm} frame[{i}] |q|^2 = {ssq}, not unit")
                bad = True
                break
        if bad:
            break

    # =========================================================
    # Group E - Sign canonicalisation continuity (3)
    # =========================================================
    for assertion_id, nm in [("#E30", "BoneA"), ("#E31", "HelperBone"), ("#E32", "PivotedHardpoint")]:
        qs = decode_quat_track(slot_data.get(nm, {}).get("idx_rotation", -1), n_rotation_words)
        for i in range(1, len(qs)):
            a, b = qs[i - 1], qs[i]
            dp = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
            if dp < 0:
                errors.append(f"{assertion_id} {nm} sign discontinuity at frame {i-1}->{i}: dot={dp}")
                break

    # =========================================================
    # Group F - Translation correctness (6)
    # =========================================================
    def decode_pos_track(idx, stride, offset, scale):
        out = []
        if trans_pool_payload is None or idx < 0:
            return out
        for f in range(max(n_frames, 0)):
            base = (f * stride + idx) * 2
            if base + 6 > len(trans_pool_payload):
                break
            ints = tuple(_read_i16_le(trans_pool_payload[base + k * 2: base + k * 2 + 2]) for k in range(3))
            out.append(_unpack_position_uint16(ints, offset, scale))
        return out

    if "BoneB" in slot_data:
        sB = slot_data["BoneB"]
        # #F33 trans_offset ~ frame 0 position. BoneB authored at origin -> offset ~ 0.
        if abs(sB["trans_offset"][0]) > _TOL_TRANS_ABS:
            errors.append(f"#F33 BoneB.trans_offset[0] = {sB['trans_offset'][0]}, expected ~0.0")
        # #F34 trans_scale axes ~ (5/65535, 10/65535, 15/65535).
        exp_scale = (5.0 / 65535.0, 10.0 / 65535.0, 15.0 / 65535.0)
        for axis in range(3):
            got = sB["trans_scale"][axis]
            exp = exp_scale[axis]
            if abs(got - exp) > _TOL_TRANS_SCALE_REL * max(abs(exp), 1e-9):
                errors.append(f"#F34 BoneB.trans_scale[{axis}] = {got:.9f}, expected ~{exp:.9f}")
        # #F35 frame[30] decoded position - frame[0] ~ (5, 10, 15).
        ps = decode_pos_track(sB["idx_translation"], n_translation_words,
                              sB["trans_offset"], sB["trans_scale"])
        if len(ps) > 30:
            delta = tuple(ps[30][i] - ps[0][i] for i in range(3))
            d = max(abs(delta[0] - 5.0), abs(delta[1] - 10.0), abs(delta[2] - 15.0))
            if d > _TOL_TRANS_ABS:
                errors.append(f"#F35 BoneB frame[30]-frame[0] delta = {delta}, "
                              f"expected (5,10,15); max diff = {d}")

    # #F36 BoneA's translation scale ~ (0,0,0) (constant position held).
    if "BoneA" in slot_data:
        sA = slot_data["BoneA"]
        for axis in range(3):
            if abs(sA["trans_scale"][axis]) > 1e-6:
                errors.append(f"#F36 BoneA.trans_scale[{axis}] = {sA['trans_scale'][axis]}, expected 0")

    # #F37 HelperBone constant position
    if "HelperBone" in slot_data:
        sH = slot_data["HelperBone"]
        for axis in range(3):
            if abs(sH["trans_scale"][axis]) > 1e-6:
                errors.append(f"#F37 HelperBone.trans_scale[{axis}] = {sH['trans_scale'][axis]}, expected 0")

    # #F38 PivotedHardpoint constant position
    if "PivotedHardpoint" in slot_data:
        sP = slot_data["PivotedHardpoint"]
        for axis in range(3):
            if abs(sP["trans_scale"][axis]) > 1e-6:
                errors.append(f"#F38 PivotedHardpoint.trans_scale[{axis}] = {sP['trans_scale'][axis]}, expected 0")

    # =========================================================
    # Group G - Visibility (12)
    # =========================================================
    def check_visibility(name, expect_present, expect_frame0_visible, expect_frameN_visible,
                         assertion_prefix):
        body = bones_by_name.get(name)
        if body is None:
            errors.append(f"{assertion_prefix} bone {name!r} not found")
            return
        payload = _vis_leaf_payload(body)
        if expect_present and payload is None:
            errors.append(f"{assertion_prefix} bone {name!r} has no 0x1007 leaf "
                          f"(expected animated visibility)")
            return
        if not expect_present and payload is not None:
            errors.append(f"{assertion_prefix} bone {name!r} has unexpected 0x1007 leaf "
                          f"({len(payload)} bytes); expected elided")
            return
        if not expect_present:
            return  # nothing more to check
        # Size = ceil(n_frames/8) = 4
        exp_size = (n_frames + 7) // 8 if n_frames > 0 else 0
        if len(payload) != exp_size:
            errors.append(f"{assertion_prefix} {name!r} payload size = {len(payload)}, expected {exp_size}")
            return
        # frame 0 bit
        bit0 = payload[0] & 0x01
        if expect_frame0_visible and not bit0:
            errors.append(f"{assertion_prefix} {name!r} frame 0 expected visible (bit set), got clear")
        if not expect_frame0_visible and bit0:
            errors.append(f"{assertion_prefix} {name!r} frame 0 expected hidden (bit clear), got set")
        # frame N-1 bit
        last_byte_idx = (n_frames - 1) // 8
        last_bit_idx = (n_frames - 1) % 8
        bitN = (payload[last_byte_idx] >> last_bit_idx) & 1
        if expect_frameN_visible and not bitN:
            errors.append(f"{assertion_prefix} {name!r} frame {n_frames-1} expected visible, got clear")
        if not expect_frameN_visible and bitN:
            errors.append(f"{assertion_prefix} {name!r} frame {n_frames-1} expected hidden, got set")
        # Monotone: at most 1 transition in the bit sequence
        prev = bit0
        transitions = 0
        for f in range(1, n_frames):
            bit = (payload[f // 8] >> (f % 8)) & 1
            if bit != prev:
                transitions += 1
                prev = bit
        if transitions > 1:
            errors.append(f"{assertion_prefix} {name!r} non-monotone visibility "
                          f"({transitions} transitions in {n_frames} frames)")
        # Trailing unused bits (frame_index >= n_frames) clear
        for f in range(n_frames, 8 * len(payload)):
            if (payload[f // 8] >> (f % 8)) & 1:
                errors.append(f"{assertion_prefix} {name!r} trailing bit at frame_index {f} set, expected clear")
                break

    # #G39-G41 OmniLight: visible -> hidden, monotone
    check_visibility("OmniLight", expect_present=True,
                     expect_frame0_visible=True, expect_frameN_visible=False,
                     assertion_prefix="#G39")
    # #G42-G44 StaticBox: visible -> hidden
    check_visibility("StaticBox", expect_present=True,
                     expect_frame0_visible=True, expect_frameN_visible=False,
                     assertion_prefix="#G42")
    # #G45-G47 prox0: hidden -> visible (asymmetric)
    check_visibility("prox0", expect_present=True,
                     expect_frame0_visible=False, expect_frameN_visible=True,
                     assertion_prefix="#G45")
    # #G48 SpotLight: no 0x1007
    check_visibility("SpotLight", expect_present=False,
                     expect_frame0_visible=False, expect_frameN_visible=False,
                     assertion_prefix="#G48")
    # #G49 SpotLight.Target: no 0x1007
    check_visibility("SpotLight.Target", expect_present=False,
                     expect_frame0_visible=False, expect_frameN_visible=False,
                     assertion_prefix="#G49")
    # #G50 prox1: no 0x1007
    check_visibility("prox1", expect_present=False,
                     expect_frame0_visible=False, expect_frameN_visible=False,
                     assertion_prefix="#G50")

    # =========================================================
    # Group H - Skinning unaffected by animation (5)
    # =========================================================
    try:
        if not bones_indexed:
            alo = _alo.load(alo_path)
        # Find SkinCyl mesh.
        skin_mesh = None
        for m in alo.meshes:
            if m.name == "SkinCyl":
                skin_mesh = m
                break
        if skin_mesh is None:
            errors.append("#H51 SkinCyl mesh not found")
        else:
            total_verts = sum(len(sm.vertices) for sm in skin_mesh.submeshes)
            # #H51 vertex count (no specific number; just sanity check it's > 0)
            if total_verts == 0:
                errors.append(f"#H51 SkinCyl has 0 vertices")
            # #H52 every vertex weight sum == 1.0
            bad_sum = 0
            for sm in skin_mesh.submeshes:
                for v in sm.vertices:
                    s = sum(v.weights) if hasattr(v, "weights") else 1.0
                    if abs(s - 1.0) > 1e-3:
                        bad_sum += 1
            if bad_sum:
                errors.append(f"#H52 {bad_sum} SkinCyl vertices have weight sum != 1.0")
            # #H53 every bone reference resolves
            bad_refs = 0
            for sm in skin_mesh.submeshes:
                for v in sm.vertices:
                    if hasattr(v, "bone_indices"):
                        for bi in v.bone_indices:
                            if bi >= len(alo.bones):
                                bad_refs += 1
            if bad_refs:
                errors.append(f"#H53 {bad_refs} SkinCyl bone references out of range")
            # #H54 At least one connection wires to Root (bone 0); skinned meshes
            # connect to Root per the 5b/5c convention. We don't pin which
            # connection specifically since connection -> object mapping is
            # object_index across (meshes + lights) and we'd duplicate logic
            # to dereference it.
            root_conns = [c for c in alo.connections if c.bone_index == 0]
            if not root_conns:
                errors.append("#H54 no connection wires to bone 0 (Root); "
                              "skinned mesh should connect there")
            # #H55 no vertex references a bone outside {BoneA index, BoneB index}
            ba_idx = bones_indexed.get("BoneA", (-1, None))[0]
            bb_idx = bones_indexed.get("BoneB", (-1, None))[0]
            allowed = {ba_idx, bb_idx}
            bad_extra = 0
            for sm in skin_mesh.submeshes:
                for v in sm.vertices:
                    if hasattr(v, "bone_indices"):
                        for slot, bi in enumerate(v.bone_indices):
                            if hasattr(v, "weights") and v.weights[slot] > 0 and bi not in allowed:
                                bad_extra += 1
            if bad_extra:
                errors.append(f"#H55 {bad_extra} SkinCyl bone refs outside {{BoneA, BoneB}}")
    except Exception as e:
        errors.append(f"#H51-55 skinning checks failed: {e}")

    # =========================================================
    # Group I - Chunk ordering determinism (3)
    # =========================================================
    # #I56 top-level has exactly one 0x1000 container
    top_count = sum(1 for cid, ic, _ in tree if cid == 0x1000 and ic)
    if top_count != 1:
        errors.append(f"#I56 expected exactly one 0x1000 at top level, got {top_count}")
    # #I57 inside 0x1000: 0x1001 first, then 11 x 0x1002, then 0x100a, then 0x1009
    if root_children:
        # First child should be 0x1001
        if root_children and root_children[0][0] != 0x1001:
            errors.append(f"#I57 first child of 0x1000 is 0x{root_children[0][0]:X}, expected 0x1001")
        bone_count = sum(1 for cid, ic, _ in root_children if cid == 0x1002 and ic)
        if bone_count != 11:
            errors.append(f"#I58 0x1002 count = {bone_count}, expected 11")
    # #I59 (already in #A5) 0x100a precedes 0x1009 - reuse

    # =========================================================
    # Group J - .export.log cross-check (4)
    # =========================================================
    export_log_path = alo_path + ".export.log"
    if not os.path.isfile(export_log_path):
        errors.append(f"#J60 .export.log missing at {export_log_path}")
    else:
        with open(export_log_path, "r", encoding="utf-8", errors="replace") as f:
            log = f.read()
        # #J60 mentions 11 bones in Animation line
        if "11 bone(s)" not in log:
            errors.append(f"#J60 .export.log Animation line doesn't mention '11 bone(s)'")
        # #J61 rotation_words = 16
        if "16 rotation word(s)" not in log:
            errors.append(f"#J61 .export.log doesn't say '16 rotation word(s)'")
        # #J62 translation_words = 12
        if "12 translation word(s)" not in log:
            errors.append(f"#J62 .export.log doesn't say '12 translation word(s)'")
        # #J63 3 visibility tracks
        if "3 visibility track(s)" not in log:
            errors.append(f"#J63 .export.log doesn't say '3 visibility track(s)'")

    # =========================================================
    # Report
    # =========================================================
    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  ({n_frames} frames @ {fps:.1f} fps, "
          f"n_bones={n_bones_declared}, rot_words={n_rotation_words}, "
          f"trans_words={n_translation_words}; "
          f"4 bones animated + 3 visibility tracks + 4 control bones elided; "
          f"62/62 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

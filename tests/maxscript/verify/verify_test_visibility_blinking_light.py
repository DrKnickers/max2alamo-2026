"""verify_test_visibility_blinking_light.py - Phase 8d visibility-pass acceptance.

Pins (15 assertions across 3 groups):
  A. Structural - .ala sibling exists; top-level 0x1000 container; n_frames=31;
     FoC framing (mini-chunks 11/12/13 in 0x1001); no unknown leaf IDs inside
     per-bone 0x1002 containers.
  B. Visibility chunk emission - BlinkingLight bone exists; has a 0x1007 leaf
     of size ceil(n_frames/8); AlwaysVisible bone exists; AlwaysVisible has
     NO 0x1007 (constant-visible elision); no other bone has 0x1007.
  C. Bit-packing semantics - frame 0 (start key = TRUE) is set in the payload;
     frame n_frames-1 (end key = FALSE) is clear; the transition exists
     (payload is neither all-FF nor all-00); trailing unused bits beyond
     frame n_frames-1 are clear (no garbage padding).

Why these specific assertions: Max 2026's on_off visibility controller can't
be replaced with a clean bezier_float via any documented MaxScript path
(verified empirically during Phase 8d development), and INode::GetVisibility
samples it as a float while MaxScript reads it as a thresholded bool, so the
exact transition frame is Max-implementation-defined. The test pins the
walker's pipeline (pack_visibility_bits + >= 0.5 threshold + emission gate +
visibility_map plumbing) via endpoint values + payload invariants instead.
"""
import math
import os
import struct
import sys

_VIS_CHUNK_ID = 0x1007
# Documented allowed leaf IDs inside a per-bone 0x1002 container
# (per ala_reader.cpp:203 and ala_writer.cpp:178).
_ALLOWED_BONE_LEAF_IDS = {0x1003, 0x1004, 0x1005, 0x1006, 0x1007, 0x1008}


def _read_u32_le(b): return struct.unpack_from("<I", b, 0)[0]
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


def _vis_leaf_payload(bone_body):
    for cid, is_container, body in bone_body:
        if cid == _VIS_CHUNK_ID and not is_container:
            return body
    return None


def main(alo_path):
    errors = []
    ala_path = (alo_path[:-4] + ".ala"
                if alo_path.lower().endswith(".alo") else alo_path + ".ala")

    # #A1 .ala exists alongside .alo.
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

    # #A2 top-level 0x1000 container.
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
    for cid, is_container, body in root_children:
        if cid == 0x1001 and not is_container:
            info_payload = body
        elif cid == 0x1002 and is_container:
            bone_containers.append(body)

    # #A3 n_frames + #A4 FoC framing.
    n_frames = -1
    fps = 0.0
    n_bones_declared = -1
    saw_foc_11 = saw_foc_12 = saw_foc_13 = False
    if info_payload is not None:
        for mid, body in _walk_minichunks(info_payload):
            if   mid ==  1 and len(body) >= 4: n_frames = _read_u32_le(body)
            elif mid ==  2 and len(body) >= 4: fps = _read_f32_le(body)
            elif mid ==  3 and len(body) >= 4: n_bones_declared = _read_u32_le(body)
            elif mid == 11: saw_foc_11 = True
            elif mid == 12: saw_foc_12 = True
            elif mid == 13: saw_foc_13 = True

    if n_frames != 31:
        errors.append(f"#A3 n_frames = {n_frames}, expected 31")
    if not (saw_foc_11 and saw_foc_12 and saw_foc_13):
        errors.append(f"#A4 FoC mini-chunks (11/12/13) not all present "
                      f"(11={saw_foc_11}, 12={saw_foc_12}, 13={saw_foc_13})")
    # #A5 only documented leaf IDs inside per-bone 0x1002 containers.
    for bone_body in bone_containers:
        for cid, is_container, _ in bone_body:
            if not is_container and cid not in _ALLOWED_BONE_LEAF_IDS:
                errors.append(f"#A5 unknown leaf 0x{cid:X} inside 0x1002 (allowed: "
                              f"{', '.join(f'0x{x:X}' for x in sorted(_ALLOWED_BONE_LEAF_IDS))})")
                break

    # Locate the test bones.
    blinking = None
    always_visible = None
    other_bones = []
    for bone_body in bone_containers:
        name = _bone_name(bone_body)
        if name == "BlinkingLight":
            blinking = bone_body
        elif name == "AlwaysVisible":
            always_visible = bone_body
        else:
            other_bones.append((name, bone_body))

    # Group B - Visibility chunk emission
    # #B6 BlinkingLight bone exists.
    if blinking is None:
        errors.append("#B6 BlinkingLight bone not found in 0x1002 containers")
    # #B9 AlwaysVisible bone exists.
    if always_visible is None:
        errors.append("#B9 AlwaysVisible bone not found in 0x1002 containers")

    blinking_payload = None
    expected_size = (max(n_frames, 0) + 7) // 8
    if blinking is not None:
        blinking_payload = _vis_leaf_payload(blinking)
        # #B7 BlinkingLight has 0x1007 leaf.
        if blinking_payload is None:
            errors.append("#B7 BlinkingLight has no 0x1007 leaf "
                          "(frame 30 is hidden -> visibility track expected)")
        # #B8 0x1007 payload size = ceil(n_frames/8).
        elif len(blinking_payload) != expected_size:
            errors.append(f"#B8 0x1007 payload size = {len(blinking_payload)}, "
                          f"expected {expected_size} (ceil({n_frames}/8))")

    # #B10 AlwaysVisible has NO 0x1007 leaf (constant-visible elision).
    if always_visible is not None:
        av_payload = _vis_leaf_payload(always_visible)
        if av_payload is not None:
            errors.append(f"#B10 AlwaysVisible has a 0x1007 leaf "
                          f"({len(av_payload)} bytes), expected none "
                          f"(constant-visible elision)")

    # #B11 No other bone has a 0x1007 chunk.
    for name, bone_body in other_bones:
        other_payload = _vis_leaf_payload(bone_body)
        if other_payload is not None:
            errors.append(f"#B11 bone {name!r} has unexpected 0x1007 leaf "
                          f"({len(other_payload)} bytes)")

    # Group C - Bit-packing semantics
    if blinking_payload is not None and len(blinking_payload) == expected_size \
            and n_frames > 0:
        # #C12 frame 0 (start key = TRUE) is SET (LSB-first per byte).
        first_bit = blinking_payload[0] & 0x01
        if not first_bit:
            errors.append(f"#C12 frame 0 (byte 0 bit 0) is clear, "
                          f"expected SET (key at frame 0 is TRUE); "
                          f"b0=0x{blinking_payload[0]:02X}")
        # #C13 frame n_frames-1 (end key = FALSE) is CLEAR.
        last_byte_idx = (n_frames - 1) // 8
        last_bit_idx  = (n_frames - 1) % 8
        last_bit = (blinking_payload[last_byte_idx] >> last_bit_idx) & 1
        if last_bit:
            errors.append(f"#C13 frame {n_frames - 1} (byte {last_byte_idx} bit "
                          f"{last_bit_idx}) is SET, expected CLEAR (key at "
                          f"frame {n_frames - 1} is FALSE); "
                          f"byte=0x{blinking_payload[last_byte_idx]:02X}")
        # #C14 transition exists AND is monotone visible-then-hidden in
        # LSB reading order. Catches three breakages:
        #   - all-visible (no transition): n_hidden == 0
        #   - all-hidden  (no transition): n_visible == 0
        #   - MSB-first encoding: reading bits in LSB iteration order produces
        #     a non-monotone sequence (visible-after-hidden), because MSB-first
        #     scrambles the per-byte order of monotone-authored visibility.
        # The test scene authored TRUE at frame 0 and FALSE at frame n_frames-1
        # with a smooth controller in between, so the on-disk pattern MUST be
        # monotone under the correct LSB-first convention.
        n_visible = 0
        n_hidden  = 0
        saw_hidden_first = -1
        first_visible_after_hidden = -1
        for f in range(n_frames):
            bit = (blinking_payload[f // 8] >> (f % 8)) & 1
            if bit:
                n_visible += 1
                if saw_hidden_first >= 0 and first_visible_after_hidden < 0:
                    first_visible_after_hidden = f
            else:
                n_hidden += 1
                if saw_hidden_first < 0:
                    saw_hidden_first = f
        if n_visible == 0 or n_hidden == 0:
            errors.append(f"#C14 no transition: n_visible={n_visible}, "
                          f"n_hidden={n_hidden} -- the test authored TRUE at "
                          f"frame 0 and FALSE at frame {n_frames - 1}, so both "
                          f"populations must be > 0")
        elif first_visible_after_hidden >= 0:
            errors.append(f"#C14 non-monotone visibility: hidden first appears "
                          f"at frame {saw_hidden_first} but a visible bit "
                          f"reappears at frame {first_visible_after_hidden} -- "
                          f"either the controller wasn't monotone (bug in test "
                          f"authoring) or the bit-packing is MSB-first (LSB-"
                          f"first iteration sees scrambled bytes as alternating)")
        # #C15 trailing unused bits (frame_index >= n_frames) are CLEAR.
        for f in range(n_frames, 8 * len(blinking_payload)):
            disk_bit = (blinking_payload[f // 8] >> (f % 8)) & 1
            if disk_bit:
                errors.append(f"#C15 trailing bit at byte {f // 8} bit {f % 8} "
                              f"(frame_index {f} beyond n_frames {n_frames}) "
                              f"is SET, expected CLEAR")
                break
    else:
        if blinking is not None:
            errors.append("#C12-15 skipped (no usable 0x1007 payload; "
                          "see #B7/#B8)")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    n_other = len(other_bones)
    payload_str = (" ".join(f"{b:02X}" for b in blinking_payload)
                   if blinking_payload is not None else "<elided>")
    print(f"OK  ({n_frames} frames @ {fps:.1f} fps, n_bones={n_bones_declared}; "
          f"BlinkingLight 0x1007=[{payload_str}] "
          f"(visible={n_visible}, hidden={n_hidden}), "
          f"AlwaysVisible elided, {n_other} other bone(s) with no 0x1007; "
          f"15/15 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

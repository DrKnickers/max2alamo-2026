"""verify_test_phase8_visibility_propagation.py - Phase 8f propagation regression.

Pins the behavior of the "Propagate visibility to descendants" button:

  - Selected node (ParentBone) has authored visibility animation.
  - Three direct children (Child1/2/3) have their visibility controllers
    cloned from the parent post-propagation -- they should emit 0x1007
    chunks with byte-identical payloads to ParentBone's.
  - OutsideSubtree is parented to Root (NOT to ParentBone) -- propagation
    must not reach arbitrary scene nodes outside the selected subtree.
    Pinned by asserting OutsideSubtree has NO 0x1007 (constant-visible).

Total: 7 assertions across 3 groups (structural / propagation copy /
scope-of-propagation).
"""
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))


_VIS_CHUNK_ID = 0x1007


def _read_u32_le(b): return struct.unpack_from("<I", b, 0)[0]


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


def _walk_minichunks(payload):
    cur = 0
    while cur + 2 <= len(payload):
        cid = payload[cur]; sz = payload[cur + 1]
        if cur + 2 + sz > len(payload):
            raise ValueError(f"truncated mini-chunk at {cur}")
        yield cid, payload[cur + 2:cur + 2 + sz]
        cur += 2 + sz


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


def _vis_payload(bone_body):
    for cid, is_container, body in bone_body:
        if cid == _VIS_CHUNK_ID and not is_container:
            return bytes(body)
    return None


def main(alo_path):
    errors = []
    ala_path = (alo_path[:-4] + ".ala"
                if alo_path.lower().endswith(".alo") else alo_path + ".ala")

    # #A1
    if not os.path.isfile(ala_path):
        print(f"FAIL: expected .ala at {ala_path}", file=sys.stderr)
        return 1

    with open(ala_path, "rb") as f:
        data = f.read()
    tree = _parse_chunk_tree(data)

    root = None
    for cid, ic, body in tree:
        if cid == 0x1000 and ic:
            root = body
            break
    if not root:
        print("FAIL: no top-level 0x1000", file=sys.stderr)
        return 1

    bones_by_name = {}
    for cid, ic, body in root:
        if cid == 0x1002 and ic:
            bones_by_name[_bone_name(body)] = body

    expected = {"ParentBone", "Child1", "Child2", "Child3", "OutsideSubtree"}
    missing = expected - bones_by_name.keys()
    if missing:
        errors.append(f"#A2 missing bones: {missing}")

    # #B3 ParentBone has 0x1007
    parent_payload = _vis_payload(bones_by_name.get("ParentBone", []))
    if parent_payload is None:
        errors.append("#B3 ParentBone has no 0x1007 (expected visibility track)")

    # #B4-B6 each child has 0x1007 byte-identical to parent's
    for name in ["Child1", "Child2", "Child3"]:
        body = bones_by_name.get(name)
        if body is None:
            errors.append(f"#B-{name} bone missing")
            continue
        payload = _vis_payload(body)
        if payload is None:
            errors.append(f"#B-{name} has no 0x1007 (propagation failed; "
                          f"button didn't copy parent's visibility controller)")
        elif parent_payload is not None and payload != parent_payload:
            errors.append(f"#B-{name} 0x1007 payload differs from ParentBone "
                          f"({payload[:8].hex()} vs {parent_payload[:8].hex()})")

    # #C7 OutsideSubtree (parented to Root) has NO 0x1007 — propagation
    # must not reach arbitrary scene nodes outside the selected subtree.
    se_payload = _vis_payload(bones_by_name.get("OutsideSubtree", []))
    if se_payload is not None:
        errors.append(f"#C7 OutsideSubtree (parented to Root, not ParentBone) "
                      f"has unexpected 0x1007 ({len(se_payload)} bytes); "
                      f"propagation leaked outside the selected subtree")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  (propagation copied ParentBone's visibility to 3 children "
          f"byte-identical; OutsideSubtree correctly elided after spot-edit "
          f"override; 7/7 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

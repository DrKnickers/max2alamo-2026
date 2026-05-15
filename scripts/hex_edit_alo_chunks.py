"""Hex-edit specific chunks in an .alo file. Built for the Phase 10 prereq
Tier 4 experiment (issue #75 / docs/plans/phase-10-prereq.md) — produces
variant .alo files that differ from a known-good control by exactly one
targeted chunk-level change, so any rendering difference observed in
AloViewer / the engine is attributable to that change alone.

Operations
----------

  --rewrite-vfmt <input.alo> <new_name> <output.alo> [--only-current STR]
      Find every leaf chunk with id 0x10002 (vertex-format-name cstring)
      and rewrite its payload to <new_name> + NUL. Supports arbitrary
      size changes — every enclosing container's size word is updated
      to match. With --only-current STR, only chunks whose current
      payload-cstring equals STR are rewritten (others passed through
      untouched). Useful when an .alo has mixed-format submeshes and
      you only want to test the skinned ones.

  --strip-skin-remap <input.alo> <output.alo>
      Find every leaf chunk with id 0x10006 (per-submesh skin-bone
      remap table) inside a 0x10000 submesh-data container and remove
      it. Per-AloViewer Models.cpp:155, 0x10006 is the per-submesh
      remap from local skin-bone slot to global skeleton index;
      stripping it tests whether the engine can render skinned meshes
      without one.

Determinism
-----------

Both operations preserve byte ordering everywhere they don't actively
edit, and update enclosing container size words consistently. A
round-trip through `alo_roundtrip` on a variant should produce
byte-identical output.

Reference: AloViewer src/Assets/Models.cpp:109-155 + src/RenderEngine/
DirectX9/VertexFormats.cpp for the chunk types this script targets.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

CONTAINER_FLAG = 0x80000000
SIZE_MASK = 0x7FFFFFFF
CHUNK_HEADER_SIZE = 8

CHUNK_VFMT_STR = 0x10002
CHUNK_SKIN_REMAP = 0x10006
CHUNK_SUB_DATA = 0x10000


def encode_size(payload_size: int, is_container: bool) -> int:
    return (payload_size & SIZE_MASK) | (CONTAINER_FLAG if is_container else 0)


def read_chunks(buf: bytes, start: int, end: int):
    """Yield (chunk_id, is_container, payload_size, payload_start, payload_end,
    header_offset) for every chunk in [start, end)."""
    off = start
    while off + CHUNK_HEADER_SIZE <= end:
        cid, sz = struct.unpack_from("<II", buf, off)
        is_container = bool(sz & CONTAINER_FLAG)
        size = sz & SIZE_MASK
        ps = off + CHUNK_HEADER_SIZE
        pe = ps + size
        if pe > end:
            return
        yield cid, is_container, size, ps, pe, off
        off = pe


def cstring(buf, start, end) -> str:
    sub = bytes(buf[start:end])
    n = sub.find(b"\x00")
    return sub[:n].decode("ascii", errors="replace") if n >= 0 else sub.decode("ascii", errors="replace")


def find_chunks(buf, predicate):
    """Return list of (header_offset, payload_size, ancestor_header_offsets)
    for every chunk where predicate(chunk_id, is_container, payload_bytes)
    returns True. ancestor_header_offsets is the list of containing-
    container header offsets from outermost to innermost."""
    matches = []

    def walk(start, end, ancestors):
        for cid, ic, sz, ps, pe, hoff in read_chunks(buf, start, end):
            if predicate(cid, ic, buf[ps:pe]):
                matches.append((hoff, sz, list(ancestors)))
            if ic:
                walk(ps, pe, ancestors + [hoff])

    walk(0, len(buf), [])
    return matches


def replace_chunk_payload(buf: bytearray, header_offset: int, new_payload: bytes, ancestor_header_offsets: list[int]) -> None:
    """Replace the chunk at header_offset with one carrying new_payload.
    Updates the chunk's size word and every ancestor container's size word
    by the size delta."""
    cid, sz_with_flag = struct.unpack_from("<II", buf, header_offset)
    is_container = bool(sz_with_flag & CONTAINER_FLAG)
    old_size = sz_with_flag & SIZE_MASK
    new_size = len(new_payload)
    delta = new_size - old_size

    # Replace payload bytes in-place (extends or shrinks the buffer).
    payload_start = header_offset + CHUNK_HEADER_SIZE
    payload_end = payload_start + old_size
    buf[payload_start:payload_end] = new_payload

    # Update this chunk's size word.
    struct.pack_into("<II", buf, header_offset, cid, encode_size(new_size, is_container))

    # Bubble the size delta up through ancestor containers.
    if delta != 0:
        for a_hoff in ancestor_header_offsets:
            a_cid, a_sz = struct.unpack_from("<II", buf, a_hoff)
            a_ic = bool(a_sz & CONTAINER_FLAG)
            a_size = a_sz & SIZE_MASK
            struct.pack_into("<II", buf, a_hoff, a_cid, encode_size(a_size + delta, a_ic))


# ---- --rewrite-vfmt -------------------------------------------------------


def rewrite_vfmt(in_path: pathlib.Path, out_path: pathlib.Path, new_name: str,
                 only_current: str | None = None) -> int:
    """Rewrite every 0x10002 leaf payload to <new_name> + NUL. With
    only_current set, skip 0x10002 chunks whose current cstring payload
    doesn't equal only_current. Returns count of chunks rewritten."""
    src = in_path.read_bytes()
    new_payload = new_name.encode("ascii") + b"\x00"

    def is_match(cid, ic, payload_bytes):
        if cid != CHUNK_VFMT_STR or ic:
            return False
        if only_current is None:
            return True
        # Decode current payload cstring and compare.
        n = payload_bytes.find(b"\x00")
        current = (payload_bytes[:n] if n >= 0 else payload_bytes).decode("ascii", errors="replace")
        return current == only_current

    # Sort matches by header_offset DESCENDING so edits don't shift earlier
    # offsets in the buffer.
    matches = find_chunks(src, is_match)
    matches.sort(key=lambda r: -r[0])

    buf = bytearray(src)
    for hoff, _old_size, ancestors in matches:
        replace_chunk_payload(buf, hoff, new_payload, ancestors)

    out_path.write_bytes(bytes(buf))
    return len(matches)


# ---- --strip-skin-remap ---------------------------------------------------


def strip_skin_remap(in_path: pathlib.Path, out_path: pathlib.Path) -> int:
    """Remove every 0x10006 chunk inside a 0x10000 container. Returns
    count of chunks removed."""
    src = in_path.read_bytes()

    def is_match(cid, ic, _payload):
        return cid == CHUNK_SKIN_REMAP and not ic

    matches = find_chunks(src, is_match)
    # We additionally need to confirm each match has a 0x10000 ancestor;
    # find_chunks's predicate doesn't see ancestor list. Filter here.
    # Re-walk to capture ancestor chunk-IDs alongside their header offsets.
    def find_with_ancestor_cids(buf):
        out_matches = []

        def walk(start, end, anc_cids, anc_hoffs):
            for cid, ic, _sz, ps, pe, hoff in read_chunks(buf, start, end):
                if cid == CHUNK_SKIN_REMAP and not ic and CHUNK_SUB_DATA in anc_cids:
                    out_matches.append((hoff, pe, list(anc_hoffs)))
                if ic:
                    walk(ps, pe, anc_cids + [cid], anc_hoffs + [hoff])

        walk(0, len(buf), [], [])
        return out_matches

    real_matches = find_with_ancestor_cids(src)
    if not real_matches:
        out_path.write_bytes(src)
        return 0

    # Sort high-offset-first so deletions don't shift earlier offsets.
    real_matches.sort(key=lambda r: -r[0])

    buf = bytearray(src)
    for hoff, pe, ancestors in real_matches:
        removed_bytes = pe - hoff
        # Update each ancestor container's size word.
        for a_hoff in ancestors:
            a_cid, a_sz = struct.unpack_from("<II", buf, a_hoff)
            a_ic = bool(a_sz & CONTAINER_FLAG)
            a_size = a_sz & SIZE_MASK
            struct.pack_into("<II", buf, a_hoff, a_cid, encode_size(a_size - removed_bytes, a_ic))
        # Delete the chunk bytes.
        del buf[hoff:pe]

    out_path.write_bytes(bytes(buf))
    return len(real_matches)


# ---- CLI ------------------------------------------------------------------


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="op", required=True)

    p_vfmt = sub.add_parser("rewrite-vfmt", help="rewrite 0x10002 payload(s)")
    p_vfmt.add_argument("input")
    p_vfmt.add_argument("new_name")
    p_vfmt.add_argument("output")
    p_vfmt.add_argument("--only-current", default=None,
                        help="only rewrite chunks whose current payload equals this string")

    p_strip = sub.add_parser("strip-skin-remap", help="remove 0x10006 chunks inside 0x10000")
    p_strip.add_argument("input")
    p_strip.add_argument("output")

    args = p.parse_args()
    if args.op == "rewrite-vfmt":
        n = rewrite_vfmt(pathlib.Path(args.input), pathlib.Path(args.output),
                         args.new_name, only_current=args.only_current)
        filt = f" (only chunks currently == '{args.only_current}')" if args.only_current else ""
        print(f"# rewrote {n} 0x10002 chunk(s) -> '{args.new_name}'{filt}", file=sys.stderr)
    elif args.op == "strip-skin-remap":
        n = strip_skin_remap(pathlib.Path(args.input), pathlib.Path(args.output))
        print(f"# stripped {n} 0x10006 skin-remap chunk(s)", file=sys.stderr)


if __name__ == "__main__":
    # Allow --op-name form for ergonomic invocation.
    argv = sys.argv[1:]
    if argv and argv[0].startswith("--") and argv[0][2:] in ("rewrite-vfmt", "strip-skin-remap"):
        argv[0] = argv[0][2:]
        sys.argv = [sys.argv[0]] + argv
    main()

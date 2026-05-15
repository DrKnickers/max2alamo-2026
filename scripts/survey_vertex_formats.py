"""Survey (shader-name, 0x10002 vertex-format string, vertex chunk ID) across
the corpus to feed the issue #75 (Phase 10) investigation.

Walks every .alo under tests/corpus/, descends 0x600 -> 0x400 (mesh) ->
0x10100 (submesh material) + 0x10000 (submesh data), and records:
  - shader_name  : cstring payload of 0x10101 inside 0x10100
  - vfmt_string  : cstring payload of 0x10002 inside 0x10000
  - vchunk_id    : the chunk ID of the vertex-data chunk inside 0x10000
                   (0x10007 = rev 2 144 B/vertex; 0x10005 / 0x10006 are
                   earlier revs sometimes observed in vanilla)
  - bytes_per_vx : vertex-chunk payload size / vertex count from 0x10001

Output: CSV to stdout, plus a tally of unique (shader_name, vfmt_string,
vchunk_id) triplets.
"""

from __future__ import annotations

import collections
import pathlib
import struct
import sys

CONTAINER_FLAG = 0x80000000
SIZE_MASK = 0x7FFFFFFF

# Chunk IDs we care about for the submesh walk.
CHUNK_MESH_LIST   = 0x600
CHUNK_MESH        = 0x400
CHUNK_SUB_MAT     = 0x10100
CHUNK_SUB_DATA    = 0x10000
CHUNK_SHADER_NAME = 0x10101
CHUNK_VFMT_STR    = 0x10002
CHUNK_SUB_SIZES   = 0x10001
# Vertex-data chunk IDs per AloViewer Models.cpp:109 (`type == 0x10007 || type == 0x10005`).
# Important: 0x10006 is NOT a vertex chunk -- per AloViewer Models.cpp:155 it's a
# per-mesh skin-bone REMAP table (a flat uint32 list mapping local skin-bone
# slots to global skeleton indices). An earlier draft of this script captured
# 0x10006 as a vertex chunk and produced bogus 0-byte/vertex rows for ~7% of
# the corpus.
VERTEX_CHUNK_IDS  = {0x10005, 0x10007}
CHUNK_SKIN_REMAP  = 0x10006


def read_chunks(buf: bytes, start: int, end: int):
    """Yield (id, is_container, payload_start, payload_end) inside [start, end)."""
    off = start
    while off < end:
        if off + 8 > end:
            return
        cid, sz_with_flag = struct.unpack_from("<II", buf, off)
        is_container = bool(sz_with_flag & CONTAINER_FLAG)
        size = sz_with_flag & SIZE_MASK
        payload_start = off + 8
        payload_end = payload_start + size
        if payload_end > end:
            return
        yield cid, is_container, payload_start, payload_end
        off = payload_end


def cstring(buf: bytes, start: int, end: int) -> str:
    sub = buf[start:end]
    nul = sub.find(b"\x00")
    if nul < 0:
        return sub.decode("ascii", errors="replace")
    return sub[:nul].decode("ascii", errors="replace")


def walk_submeshes(buf: bytes, start: int, end: int, records: list, src: str):
    """Walk one 0x400 mesh container's children, collecting per-submesh records.

    Submesh material (0x10100) and submesh data (0x10000) appear as
    siblings, alternating per submesh. So we pair them up by order.
    """
    pending_shader: str | None = None
    for cid, is_container, ps, pe in read_chunks(buf, start, end):
        if cid == CHUNK_SUB_MAT and is_container:
            # Pull the shader name from 0x10101 inside.
            shader = ""
            for c2, _ic, ps2, pe2 in read_chunks(buf, ps, pe):
                if c2 == CHUNK_SHADER_NAME:
                    shader = cstring(buf, ps2, pe2)
                    break
            pending_shader = shader
        elif cid == CHUNK_SUB_DATA and is_container:
            vfmt = ""
            vchunk_id: int | None = None
            vchunk_size = 0
            vertex_count = 0
            has_skin_remap = False
            for c2, _ic, ps2, pe2 in read_chunks(buf, ps, pe):
                if c2 == CHUNK_VFMT_STR:
                    vfmt = cstring(buf, ps2, pe2)
                elif c2 == CHUNK_SUB_SIZES:
                    # First u32 = vertex count.
                    if pe2 - ps2 >= 4:
                        vertex_count = struct.unpack_from("<I", buf, ps2)[0]
                elif c2 in VERTEX_CHUNK_IDS:
                    vchunk_id = c2
                    vchunk_size = pe2 - ps2
                elif c2 == CHUNK_SKIN_REMAP:
                    has_skin_remap = True
            if pending_shader is None:
                pending_shader = "(no-material-sibling)"
            bpv = (vchunk_size // vertex_count) if vertex_count else 0
            records.append((src, pending_shader, vfmt, vchunk_id, vertex_count, bpv, has_skin_remap))
            pending_shader = None


def walk_file(path: pathlib.Path, records: list):
    buf = path.read_bytes()
    end = len(buf)
    src = path.name
    # Top-level: descend into 0x600 (mesh list), then 0x400 meshes.
    for cid, is_container, ps, pe in read_chunks(buf, 0, end):
        if cid == CHUNK_MESH_LIST and is_container:
            for c2, ic2, ps2, pe2 in read_chunks(buf, ps, pe):
                if c2 == CHUNK_MESH and ic2:
                    walk_submeshes(buf, ps2, pe2, records, src)
        elif cid == CHUNK_MESH and is_container:
            # Some files put 0x400 at the top level.
            walk_submeshes(buf, ps, pe, records, src)


def main():
    if len(sys.argv) < 2:
        print("usage: survey_vertex_formats.py <corpus_root>", file=sys.stderr)
        sys.exit(2)
    root = pathlib.Path(sys.argv[1])
    records: list = []
    files = sorted(root.rglob("*.alo")) + sorted(root.rglob("*.ALO"))
    # de-dup case-insensitively (Windows filesystem)
    seen = set()
    uniq = []
    for f in files:
        k = str(f).lower()
        if k in seen: continue
        seen.add(k)
        uniq.append(f)
    for f in uniq:
        try:
            walk_file(f, records)
        except Exception as e:
            print(f"# parse-error {f.name}: {e}", file=sys.stderr)
    print(f"# total submesh records: {len(records)}", file=sys.stderr)
    # Tally unique (shader, vfmt, vchunk_id) triplets.
    triplet_counts: dict = collections.Counter()
    bpv_per_vchunk: dict = collections.defaultdict(collections.Counter)
    skin_remap_present = 0
    skin_remap_per_vfmt: dict = collections.Counter()
    for src, shader, vfmt, vchunk_id, _vc, bpv, has_skin_remap in records:
        triplet_counts[(shader, vfmt, vchunk_id)] += 1
        if vchunk_id is not None:
            bpv_per_vchunk[vchunk_id][bpv] += 1
        if has_skin_remap:
            skin_remap_present += 1
            skin_remap_per_vfmt[vfmt] += 1
    print(f"# unique (shader, vfmt, vchunk_id) triplets: {len(triplet_counts)}", file=sys.stderr)
    print(f"# submeshes carrying a 0x10006 skin-bone-remap chunk: {skin_remap_present}", file=sys.stderr)
    print("shader_name,vertex_format,vertex_chunk_id,count")
    for (shader, vfmt, vchunk_id), n in sorted(triplet_counts.items(),
                                               key=lambda kv: (-kv[1], kv[0])):
        vc = f"0x{vchunk_id:x}" if vchunk_id is not None else "(none)"
        print(f"{shader},{vfmt},{vc},{n}")
    print("", file=sys.stderr)
    print("# bytes-per-vertex distribution per vertex chunk ID:", file=sys.stderr)
    for cid, bpv_counts in sorted(bpv_per_vchunk.items()):
        bpvs = ", ".join(f"{bpv}B*{n}" for bpv, n in sorted(bpv_counts.items()))
        print(f"#   0x{cid:x}: {bpvs}", file=sys.stderr)
    print("# 0x10006 skin-remap distribution by vertex_format:", file=sys.stderr)
    for vfmt, n in sorted(skin_remap_per_vfmt.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"#   {vfmt}: {n}", file=sys.stderr)


if __name__ == "__main__":
    main()

"""Lightweight .alo binary parser for Max-side regression tests.

Decodes the chunk tree into a small typed structure (bones, meshes,
submeshes, materials, vertices, indices, connections) that verify_*.py
scripts can assert against. Read-only; no writer.

Vertex layout matches alamo_format/src/alo_build.cpp (rev-2 144B):
    off  0: pos.xyz
    off 12: normal.xyz
    off 24: uv0.xy
    off 32: uv1.xy
    off 40: uv2.xy
    off 48: uv3.xy
    off 56: tangent.xyz
    off 68: binormal.xyz
    off 80: color.rgb
    off 92: alpha
    off 96: unused (16B)
    off 112: boneIdx[4] (u32)
    off 128: weight[4]  (f32)
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# Chunk-size word high bit = container flag.
_CONTAINER_BIT = 0x80000000

# Material-parameter mini-chunk IDs.
_PARAM_NAME = 1
_PARAM_VALUE = 2


# ---------- typed structure -------------------------------------------------

@dataclass
class Bone:
    name: str
    parent_index: int        # 0xFFFFFFFF = no parent (root sentinel)
    visible: bool
    billboard_mode: int
    matrix: List[float]      # 12 floats, column-major (see export_scene.h)

    @property
    def translation(self) -> Tuple[float, float, float]:
        """Translation row = (matrix[3], matrix[7], matrix[11])."""
        return (self.matrix[3], self.matrix[7], self.matrix[11])

    @property
    def is_root(self) -> bool:
        return self.parent_index == 0xFFFFFFFF


@dataclass
class MaterialParam:
    name: str
    kind: str                # "float", "float4", "texture"
    float_value: Optional[float] = None
    float4_value: Optional[Tuple[float, float, float, float]] = None
    texture_value: Optional[str] = None


@dataclass
class Vertex:
    position: Tuple[float, float, float]
    normal: Tuple[float, float, float]
    uv: Tuple[float, float]
    tangent: Tuple[float, float, float]
    binormal: Tuple[float, float, float]
    color: Tuple[float, float, float]
    alpha: float
    bone_indices: Tuple[int, int, int, int]
    weights: Tuple[float, float, float, float]


@dataclass
class Submesh:
    shader_name: str = ""
    material_params: List[MaterialParam] = field(default_factory=list)
    vertices: List[Vertex] = field(default_factory=list)
    indices: List[int] = field(default_factory=list)

    def find_param(self, name: str) -> Optional[MaterialParam]:
        for p in self.material_params:
            if p.name == name:
                return p
        return None


@dataclass
class Mesh:
    name: str
    submeshes: List[Submesh] = field(default_factory=list)
    # Phase 5d: 0x402 mesh-info flags (each stored as a u32 0/1 in the
    # 128-byte info chunk; populated from Alamo_Geometry_Hidden /
    # Alamo_Collision_Enabled user props on the source mesh node).
    is_hidden: bool = False
    is_collision: bool = False


@dataclass
class Connection:
    object_index: int
    bone_index: int


@dataclass
class Alo:
    bones: List[Bone] = field(default_factory=list)
    meshes: List[Mesh] = field(default_factory=list)
    connections: List[Connection] = field(default_factory=list)
    file_size: int = 0

    def bone_by_name(self, name: str) -> Optional[Bone]:
        for b in self.bones:
            if b.name == name:
                return b
        return None

    def mesh_by_name(self, name: str) -> Optional[Mesh]:
        for m in self.meshes:
            if m.name == name:
                return m
        return None


# ---------- chunk-tree walker ----------------------------------------------

def _walk(data: bytes, start: int, end: int):
    """Yield (chunk_id, is_container, payload_offset, payload_size, end_offset)
    for each chunk in [start, end)."""
    p = start
    while p < end:
        cid, size_word = struct.unpack_from("<II", data, p)
        size = size_word & ~_CONTAINER_BIT
        is_container = bool(size_word & _CONTAINER_BIT)
        payload_off = p + 8
        chunk_end = payload_off + size
        if chunk_end > end:
            raise ValueError(
                f"chunk 0x{cid:X} at {p}: payload runs past parent end ({chunk_end} > {end})")
        yield cid, is_container, payload_off, size, chunk_end
        p = chunk_end


def _read_cstring(data: bytes, off: int, size: int) -> str:
    return data[off:off + size].rstrip(b"\x00").decode("utf-8", errors="replace")


def _decode_mini_chunks(payload: bytes):
    """Walk (type, size, body) mini-chunks inside a 0x10103/0x10105/0x10106
    payload. Yields tuples."""
    p = 0
    while p + 2 <= len(payload):
        t = payload[p]
        sz = payload[p + 1]
        body = payload[p + 2:p + 2 + sz]
        yield t, sz, body
        p += 2 + sz


def _parse_param_chunk(cid: int, payload: bytes) -> MaterialParam:
    name = "?"
    if cid == 0x10103:
        kind = "float"
        fv: Optional[float] = None
    elif cid == 0x10106:
        kind = "float4"
        v4: Optional[Tuple[float, float, float, float]] = None
    elif cid == 0x10105:
        kind = "texture"
        tv: Optional[str] = None
    else:
        raise ValueError(f"not a known material-param chunk: 0x{cid:X}")

    for t, sz, body in _decode_mini_chunks(payload):
        if t == _PARAM_NAME:
            name = body.rstrip(b"\x00").decode("utf-8", errors="replace")
        elif t == _PARAM_VALUE:
            if cid == 0x10103:
                fv = struct.unpack("<f", body)[0]
            elif cid == 0x10106:
                v4 = struct.unpack("<4f", body)
            elif cid == 0x10105:
                tv = body.rstrip(b"\x00").decode("utf-8", errors="replace")

    p = MaterialParam(name=name, kind=kind)
    if cid == 0x10103:
        p.float_value = fv
    elif cid == 0x10106:
        p.float4_value = v4
    elif cid == 0x10105:
        p.texture_value = tv
    return p


def _parse_vertex(data: bytes, off: int) -> Vertex:
    pos = struct.unpack_from("<3f", data, off + 0)
    nrm = struct.unpack_from("<3f", data, off + 12)
    uv = struct.unpack_from("<2f", data, off + 24)
    tan = struct.unpack_from("<3f", data, off + 56)
    bin_ = struct.unpack_from("<3f", data, off + 68)
    col = struct.unpack_from("<3f", data, off + 80)
    alpha = struct.unpack_from("<f", data, off + 92)[0]
    bidx = struct.unpack_from("<4I", data, off + 112)
    wts = struct.unpack_from("<4f", data, off + 128)
    return Vertex(
        position=pos, normal=nrm, uv=uv,
        tangent=tan, binormal=bin_,
        color=col, alpha=alpha,
        bone_indices=bidx, weights=wts,
    )


def _parse_bone(data: bytes, payload_off: int, end: int) -> Optional[Bone]:
    name = "?"
    parent = 0xFFFFFFFF
    visible = True
    billboard = 0
    matrix = [1.0, 0.0, 0.0, 0.0,
              0.0, 1.0, 0.0, 0.0,
              0.0, 0.0, 1.0, 0.0]
    for cid, _, off, size, _end in _walk(data, payload_off, end):
        if cid == 0x203:
            name = _read_cstring(data, off, size)
        elif cid in (0x205, 0x206) and size >= 60:
            parent = struct.unpack_from("<I", data, off + 0)[0]
            visible = struct.unpack_from("<I", data, off + 4)[0] != 0
            billboard = struct.unpack_from("<I", data, off + 8)[0]
            matrix = list(struct.unpack_from("<12f", data, off + 12))
    return Bone(name=name, parent_index=parent, visible=visible,
                billboard_mode=billboard, matrix=matrix)


def _parse_submesh(data: bytes, material_payload, geom_payload) -> Submesh:
    sm = Submesh()
    # Material container (0x10100): 0x10101 shader name + 0x10103/5/6 param chunks
    mat_off, mat_end = material_payload
    for cid, _, off, size, _end in _walk(data, mat_off, mat_end):
        if cid == 0x10101:
            sm.shader_name = _read_cstring(data, off, size)
        elif cid in (0x10103, 0x10105, 0x10106):
            sm.material_params.append(_parse_param_chunk(cid, data[off:off + size]))

    # Geometry container (0x10000): 0x10001 sizes, 0x10007 verts, 0x10004 faces
    if geom_payload is None:
        return sm
    geom_off, geom_end = geom_payload
    vertex_count = 0
    face_count = 0
    vbuf = None
    ibuf = None
    for cid, _, off, size, _end in _walk(data, geom_off, geom_end):
        if cid == 0x10001:
            vertex_count, face_count = struct.unpack_from("<II", data, off)
        elif cid == 0x10007:
            vbuf = (off, size)
        elif cid == 0x10004:
            ibuf = (off, size)

    if vbuf is not None and vbuf[1] == vertex_count * 144:
        for i in range(vertex_count):
            sm.vertices.append(_parse_vertex(data, vbuf[0] + i * 144))
    if ibuf is not None:
        for i in range(face_count * 3):
            (idx,) = struct.unpack_from("<H", data, ibuf[0] + i * 2)
            sm.indices.append(idx)
    return sm


def _parse_mesh(data: bytes, payload_off: int, end: int) -> Mesh:
    name = "?"
    materials = []        # list of (off, end)
    geometries = []       # list of (off, end)
    is_hidden = False
    is_collision = False
    for cid, _, off, size, _end in _walk(data, payload_off, end):
        if cid == 0x401:
            name = _read_cstring(data, off, size)
        elif cid == 0x402 and size >= 40:
            # 0x402 mesh-info layout (alo_build.cpp::build_mesh_info):
            #   u32 submesh_count, 3*f32 bbox_min, 3*f32 bbox_max,
            #   u32 unused, u32 is_hidden, u32 is_collision, ...zeros.
            is_hidden    = struct.unpack_from("<I", data, off + 32)[0] != 0
            is_collision = struct.unpack_from("<I", data, off + 36)[0] != 0
        elif cid == 0x10100:
            materials.append((off, off + size))
        elif cid == 0x10000:
            geometries.append((off, off + size))
    mesh = Mesh(name=name, is_hidden=is_hidden, is_collision=is_collision)
    # Materials and geometries are siblings paired in declaration order
    # (per the Phase 4c vanilla-layout fix).
    for i, mat in enumerate(materials):
        geom = geometries[i] if i < len(geometries) else None
        mesh.submeshes.append(_parse_submesh(data, mat, geom))
    return mesh


def _parse_connection(data: bytes, payload: bytes) -> Connection:
    obj_idx = -1
    bone_idx = -1
    for t, sz, body in _decode_mini_chunks(payload):
        if t == 2:
            obj_idx = struct.unpack("<I", body)[0]
        elif t == 3:
            bone_idx = struct.unpack("<I", body)[0]
    return Connection(object_index=obj_idx, bone_index=bone_idx)


# ---------- public load() ---------------------------------------------------

def load(path: str) -> Alo:
    with open(path, "rb") as f:
        data = f.read()
    alo = Alo(file_size=len(data))

    for cid, is_container, off, size, end in _walk(data, 0, len(data)):
        if cid == 0x200 and is_container:
            # skeleton: 0x201 info + N * 0x202 bone
            for ccid, ccon, coff, csize, cend in _walk(data, off, end):
                if ccid == 0x202 and ccon:
                    alo.bones.append(_parse_bone(data, coff, cend))
        elif cid == 0x400 and is_container:
            alo.meshes.append(_parse_mesh(data, off, end))
        elif cid == 0x600 and is_container:
            for ccid, _, coff, csize, _cend in _walk(data, off, end):
                if ccid == 0x602:
                    alo.connections.append(_parse_connection(data, data[coff:coff + csize]))
    return alo

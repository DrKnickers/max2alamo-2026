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


# Phase 7a: lights and proxies.
@dataclass
class Light:
    name: str = ""
    type: int = 0                                 # 0=Omni, 1=Directional, 2=Spotlight
    color: Tuple[float, float, float] = (1., 1., 1.)
    intensity: float = 1.0
    atten_end: float = 0.0
    atten_start: float = 0.0
    hotspot: float = 0.0
    falloff: float = 0.0


@dataclass
class Proxy:
    name: str = ""
    bone_index: int = 0
    is_hidden: bool = False
    alt_decrease_stay_hidden: bool = False


@dataclass
class Alo:
    bones: List[Bone] = field(default_factory=list)
    meshes: List[Mesh] = field(default_factory=list)
    connections: List[Connection] = field(default_factory=list)
    lights: List[Light] = field(default_factory=list)
    proxies: List[Proxy] = field(default_factory=list)
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


# ---------- Tier 1 universal invariant validator ---------------------------

# Universal-mode tolerances. Calibrated against the vanilla EaW + FoC
# corpus -- whatever Petroglyph's shipped files do is by definition
# acceptable, since both engines and Mike Lankamp's importer load them
# without complaint. Vanilla file NB_DCH.ALO has a single normal at
# |n|=1.002 (float drift); 1e-2 tolerates that while still catching
# obvious corruption like a zero or wildly-magnitude normal.
_NORMAL_LEN_TOL    = 1e-2
_TANGENT_LEN_TOL   = 1e-2

# Strict-mode tolerances. Applied only to OUR walker output (where we
# control the entire pipeline and tangents come from MikkT via IGame).
# Vanilla files routinely violate these -- terrain meshes overload
# weight slots, vanilla tangents drift up to |dot(T, N)|=0.365 due to
# per-vertex MikkT averaging across face corners -- but our walker
# never does, so a violation in our output is a real regression.
_STRICT_NORMAL_LEN_TOL    = 1e-3
_STRICT_TANGENT_LEN_TOL   = 1e-3
# Smooth-shaded spheres (test_bumpcolorize_params, test_two_meshes_offset)
# hit ~0.056 at worst; the Phase 6b "wrong tangent index" bug surfaced
# as 0.39. 0.15 sits between, calibrated against our own test suite.
_STRICT_TANGENT_PERP_TOL  = 0.15
_STRICT_WEIGHT_SUM_TOL    = 1e-3


def _is_unit(v, tol):
    """True if `v` is a 3-tuple whose magnitude is ~1 OR exactly 0 (the
    'absent' sentinel the writer emits when no tangent data was available)."""
    mag = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) ** 0.5
    if mag < 1e-6:
        return True   # all-zero default (absent); not a violation
    return abs(mag - 1.0) < tol


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def validate(alo: "Alo", strict: bool = False) -> List[str]:
    """Tier-1 invariant check. Returns a list of human-readable
    violation strings; empty list means the file passes.

    Two modes:
      strict=False (default): vanilla-respecting structural checks
        only. Calibrated against the entire EaW + FoC corpus -- every
        file in tests/corpus/ passes in this mode.
      strict=True: additional checks our walker output is supposed
        to satisfy (sub-1e-3 normal/tangent length, perpendicularity
        |dot|<=0.15, per-vertex weight sum 1.0). Vanilla content
        routinely violates these (terrain meshes overload weight
        slots, vanilla tangents drift up to 0.365 from perpendicular
        due to per-vertex averaging), but our pipeline doesn't.

    The harness runs strict=True against every test export via
    validate_alo.py. The corpus sweep runs strict=False to confirm
    the validator stays compatible with PG-shipped content."""
    errors: List[str] = []
    normal_tol = _STRICT_NORMAL_LEN_TOL if strict else _NORMAL_LEN_TOL
    tangent_tol = _STRICT_TANGENT_LEN_TOL if strict else _TANGENT_LEN_TOL

    # ---- Skeleton invariants ------------------------------------------
    # Empty bones array is allowed: vanilla particle-effect files (PE_*,
    # PEM_*, PAS_*) ship with zero-bone skeletons. The 0x201 info chunk's
    # boneCount is 0 in those cases; the 0x200 container has just the
    # info leaf and no 0x202 bone chunks.
    if alo.bones:
        # When bones exist, bones[0] is the Root sentinel by Phase 4
        # convention. We don't enforce that the *only* sentinel-parent
        # bone is bones[0] -- vanilla files (W_ICERCKSLIDE00.ALO,
        # W_PROP_GENERATOR.ALO, ...) have multiple top-level sibling
        # roots, each with parent_index = 0xFFFFFFFF.
        root = alo.bones[0]
        if root.name != "Root":
            # Vanilla files don't always name bones[0] "Root" (some
            # use 'Bip01', etc.). This is informational only and
            # would have to be downgraded to a warning if we wanted
            # to enforce strictly. Currently treating as a soft check.
            pass

        for i, b in enumerate(alo.bones):
            if not b.name:
                errors.append(f"skeleton: bones[{i}] has empty name")
            if "\x00" in b.name:
                errors.append(f"skeleton: bones[{i}] name {b.name!r} has embedded null")
            if b.billboard_mode > 7:
                errors.append(f"skeleton: bones[{i}] ({b.name!r}) billboard_mode "
                              f"{b.billboard_mode} out of range [0, 7]")
            # Parents must point at a strictly earlier bone OR be the
            # no-parent sentinel (Root or a sibling top-level root).
            if b.parent_index == 0xFFFFFFFF:
                continue
            if b.parent_index >= i:
                errors.append(f"skeleton: bones[{i}] ({b.name!r}) parent_index "
                              f"{b.parent_index} not topologically sorted "
                              f"(must be < {i})")

    # ---- Mesh invariants ----------------------------------------------
    for mi, m in enumerate(alo.meshes):
        if not m.submeshes:
            errors.append(f"mesh[{mi}] ({m.name!r}): no submeshes")
            continue
        for si, sm in enumerate(m.submeshes):
            tag = f"mesh[{mi}].submesh[{si}] ({m.name!r})"
            n_verts = len(sm.vertices)
            n_idx = len(sm.indices)
            if n_idx % 3 != 0:
                errors.append(f"{tag}: index count {n_idx} is not a multiple of 3")

            # Empty submeshes (verts == 0 but indices populated) appear
            # in vanilla camera-marker / target-helper "meshes" like
            # C_CAMERA.ALO and C_SPLINETARGET.ALO. Engine tolerates them
            # by ignoring the geometry entirely; we do the same -- skip
            # the index-range check when there are no vertices.
            if n_verts > 0:
                for ii, idx in enumerate(sm.indices):
                    if idx >= n_verts:
                        errors.append(f"{tag}: indices[{ii}]={idx} >= "
                                      f"vertex count {n_verts}")
                        break  # one report per submesh is enough

            for vi, v in enumerate(sm.vertices):
                if not _is_unit(v.normal, normal_tol):
                    errors.append(f"{tag}: vert[{vi}] normal {v.normal} "
                                  f"not unit length (tol {normal_tol})")
                    break
                # bone_indices range check is structural -- if it's out
                # of range we cannot ever resolve the binding, so this
                # always-applies. We've sampled vanilla and it holds.
                for slot in range(4):
                    if alo.bones and v.bone_indices[slot] >= len(alo.bones):
                        errors.append(f"{tag}: vert[{vi}] bone_indices[{slot}]="
                                      f"{v.bone_indices[slot]} >= bone count "
                                      f"{len(alo.bones)}")
                        break

                # Strict-mode-only checks: things our walker output is
                # supposed to satisfy but vanilla content routinely
                # doesn't. See the long comment on _STRICT_* tolerances.
                if strict:
                    if not _is_unit(v.tangent, tangent_tol):
                        errors.append(f"{tag}: vert[{vi}] tangent {v.tangent} "
                                      f"not unit length (and not the zero sentinel)")
                        break
                    if not _is_unit(v.binormal, tangent_tol):
                        errors.append(f"{tag}: vert[{vi}] binormal {v.binormal} "
                                      f"not unit length (and not the zero sentinel)")
                        break
                    tmag = sum(c * c for c in v.tangent) ** 0.5
                    if tmag > 1e-6:
                        d = abs(_dot(v.tangent, v.normal))
                        if d > _STRICT_TANGENT_PERP_TOL:
                            errors.append(f"{tag}: vert[{vi}] tangent not "
                                          f"perpendicular to normal "
                                          f"(|dot|={d:.3f} > {_STRICT_TANGENT_PERP_TOL})")
                            break
                    wsum = sum(v.weights)
                    if abs(wsum - 1.0) > _STRICT_WEIGHT_SUM_TOL:
                        errors.append(f"{tag}: vert[{vi}] weights sum to "
                                      f"{wsum:.6f}, expected 1.0 "
                                      f"(±{_STRICT_WEIGHT_SUM_TOL})")
                        break
                    for slot in range(4):
                        if v.weights[slot] < 0.0:
                            errors.append(f"{tag}: vert[{vi}] weights[{slot}]="
                                          f"{v.weights[slot]} is negative")
                            break

    # ---- Light invariants (Phase 7b.1) --------------------------------
    # Universal: type in [0, 2], colour non-negative, intensity
    # non-negative, atten_start <= atten_end, hotspot <= falloff.
    # All hold across the entire vanilla corpus.
    for li, l in enumerate(alo.lights):
        tag = f"light[{li}] ({l.name!r})"
        if l.type not in (0, 1, 2):
            errors.append(f"{tag}: type {l.type} out of range [0, 2]")
        for ci, c in enumerate(l.color):
            if c < 0:
                errors.append(f"{tag}: color[{ci}]={c} is negative")
        if l.intensity < 0:
            errors.append(f"{tag}: intensity {l.intensity} is negative")
        if l.atten_start > l.atten_end + 1e-6 and l.atten_end > 0:
            # Some vanilla files have atten_start > atten_end == 0
            # (attenuation effectively disabled), which is fine. Only
            # complain when atten_end is non-zero AND start > end.
            errors.append(f"{tag}: atten_start ({l.atten_start}) > "
                          f"atten_end ({l.atten_end})")
        if l.type == 2:  # Spotlight
            if l.hotspot < 0:
                errors.append(f"{tag}: spotlight hotspot {l.hotspot} negative")
            if l.falloff < 0:
                errors.append(f"{tag}: spotlight falloff {l.falloff} negative")
            if l.hotspot > l.falloff + 1e-6:
                errors.append(f"{tag}: spotlight hotspot ({l.hotspot}) > "
                              f"falloff ({l.falloff})")
            if strict and (l.hotspot > 3.15 or l.falloff > 3.15):
                # Max enforces cone <= 180deg (pi radians); strict check.
                errors.append(f"{tag}: spotlight cone exceeds 180deg "
                              f"(hotspot={l.hotspot}, falloff={l.falloff})")

    # ---- Connection invariants ----------------------------------------
    # 0x602 connections cover meshes + lights, in (meshes ++ lights)
    # order, per Mike Lankamp's reader (alamo2max.ms:689). Phase 7a
    # generalised the writer to emit this layout.
    expected_conn = len(alo.meshes) + len(alo.lights)
    if len(alo.connections) != expected_conn:
        errors.append(f"connections: count {len(alo.connections)} != "
                      f"(meshes {len(alo.meshes)} + lights {len(alo.lights)}) "
                      f"= {expected_conn}")
    for ci, c in enumerate(alo.connections):
        if alo.bones and c.bone_index >= len(alo.bones):
            errors.append(f"connections[{ci}]: bone_index {c.bone_index} >= "
                          f"bone count {len(alo.bones)}")
        if c.object_index >= expected_conn:
            errors.append(f"connections[{ci}]: object_index {c.object_index} "
                          f">= (meshes + lights) {expected_conn}")

    return errors


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


def _parse_light(data: bytes, payload_off: int, end: int) -> Light:
    """0x1300 container: 0x1301 (name) + 0x1302 (36-byte data)."""
    light = Light()
    for cid, _, off, size, _end in _walk(data, payload_off, end):
        if cid == 0x1301:
            light.name = _read_cstring(data, off, size)
        elif cid == 0x1302 and size >= 36:
            light.type = struct.unpack_from("<I", data, off)[0]
            r, g, b = struct.unpack_from("<3f", data, off + 4)
            light.color = (r, g, b)
            light.intensity   = struct.unpack_from("<f", data, off + 16)[0]
            light.atten_end   = struct.unpack_from("<f", data, off + 20)[0]
            light.atten_start = struct.unpack_from("<f", data, off + 24)[0]
            light.hotspot     = struct.unpack_from("<f", data, off + 28)[0]
            light.falloff     = struct.unpack_from("<f", data, off + 32)[0]
    return light


def _parse_proxy(payload: bytes) -> Proxy:
    """0x603 leaf with mini-chunks 5 (name), 6 (bone), optional 7/8."""
    proxy = Proxy()
    for t, sz, body in _decode_mini_chunks(payload):
        if t == 5:
            proxy.name = body.rstrip(b"\x00").decode("utf-8", errors="replace")
        elif t == 6:
            proxy.bone_index = struct.unpack("<I", body)[0]
        elif t == 7:
            proxy.is_hidden = struct.unpack("<I", body)[0] != 0
        elif t == 8:
            proxy.alt_decrease_stay_hidden = struct.unpack("<I", body)[0] != 0
    return proxy


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
        elif cid == 0x1300 and is_container:
            alo.lights.append(_parse_light(data, off, end))
        elif cid == 0x600 and is_container:
            for ccid, _, coff, csize, _cend in _walk(data, off, end):
                if ccid == 0x602:
                    alo.connections.append(_parse_connection(data, data[coff:coff + csize]))
                elif ccid == 0x603:
                    alo.proxies.append(_parse_proxy(data[coff:coff + csize]))
    return alo

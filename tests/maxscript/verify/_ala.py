"""Lightweight .ala binary parser for harness verifiers.

Mirrors alamo_format/include/alamo_format/ala_anim.h's typed view. Sister
of _alo.py. Use:

    import _ala
    anim = _ala.load(path)
    for track in anim.bones:
        if track.idx_translation < 0:
            trans = track.trans_offset      # static (idx < 0)
        else:
            trans = _ala.unpack_translation(track, anim, frame)
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


# Mini-chunk IDs inside 0x1001 (animation info) and 0x1003 (per-bone info)
_MC_N_FRAMES         = 1
_MC_FPS              = 2
_MC_N_ROT_WORDS      = 11
_MC_N_TRANS_WORDS    = 12
_MC_BONE_NAME        = 4
_MC_SKELETON_INDEX   = 5
_MC_TRANS_OFFSET     = 6
_MC_TRANS_SCALE      = 7
_MC_IDX_TRANSLATION  = 14
_MC_IDX_ROTATION     = 16
_MC_DEFAULT_ROTATION = 17


@dataclass
class AlaBoneTrack:
    name: str = ""
    skeleton_index: int = 0
    idx_translation: int = -1
    idx_rotation: int = -1
    trans_offset: Tuple[float, float, float] = (0.0, 0.0, 0.0)
    trans_scale: Tuple[float, float, float]  = (0.0, 0.0, 0.0)
    # Packed int16 quat (x, y, z, w); divide by 32767.0 to unpack.
    default_rotation: Tuple[int, int, int, int] = (0, 0, 0, 32767)


@dataclass
class AlaAnimation:
    n_frames: int = 0
    fps: float = 30.0
    n_rotation_words: int = 0
    n_translation_words: int = 0
    bones: List[AlaBoneTrack] = field(default_factory=list)
    # Raw pool payloads, kept as int16 sequences. Use unpack_quat /
    # unpack_translation to reconstruct per-frame values.
    rotation_pool: bytes = b""
    translation_pool: bytes = b""


# ---- low-level chunk-tree helpers ----------------------------------------

def _walk_minichunks(payload: bytes):
    cur = 0
    while cur + 2 <= len(payload):
        cid = payload[cur]
        sz  = payload[cur + 1]
        if cur + 2 + sz > len(payload):
            raise ValueError(f"truncated mini-chunk at {cur}")
        yield cid, payload[cur + 2 : cur + 2 + sz]
        cur += 2 + sz


def _parse_chunk_tree(data: bytes):
    out = []
    cur = 0
    while cur < len(data):
        if cur + 8 > len(data):
            raise ValueError(f"truncated chunk header at {cur}")
        cid, sz_word = struct.unpack_from("<II", data, cur)
        size = sz_word & 0x7FFFFFFF
        is_container = bool(sz_word & 0x80000000)
        body = data[cur + 8 : cur + 8 + size]
        if is_container:
            out.append((cid, True, _parse_chunk_tree(body)))
        else:
            out.append((cid, False, body))
        cur = cur + 8 + size
    return out


def _parse_bone_info(payload: bytes) -> AlaBoneTrack:
    t = AlaBoneTrack()
    for mid, mbody in _walk_minichunks(payload):
        if mid == _MC_BONE_NAME:
            n = len(mbody)
            while n > 0 and mbody[n - 1] == 0:
                n -= 1
            t.name = mbody[:n].decode("utf-8", errors="replace")
        elif mid == _MC_SKELETON_INDEX and len(mbody) >= 4:
            t.skeleton_index = struct.unpack_from("<I", mbody, 0)[0]
        elif mid == _MC_TRANS_OFFSET and len(mbody) >= 12:
            t.trans_offset = struct.unpack_from("<3f", mbody, 0)
        elif mid == _MC_TRANS_SCALE and len(mbody) >= 12:
            t.trans_scale = struct.unpack_from("<3f", mbody, 0)
        elif mid == _MC_IDX_TRANSLATION and len(mbody) >= 2:
            t.idx_translation = struct.unpack_from("<h", mbody, 0)[0]
        elif mid == _MC_IDX_ROTATION and len(mbody) >= 2:
            t.idx_rotation = struct.unpack_from("<h", mbody, 0)[0]
        elif mid == _MC_DEFAULT_ROTATION and len(mbody) >= 8:
            t.default_rotation = struct.unpack_from("<4h", mbody, 0)
    return t


# ---- public API ---------------------------------------------------------

def load(path: str) -> AlaAnimation:
    with open(path, "rb") as f:
        data = f.read()
    tree = _parse_chunk_tree(data)
    if not tree or tree[0][0] != 0x1000 or not tree[0][1]:
        raise ValueError(f"{path}: top chunk is not 0x1000 container")

    anim = AlaAnimation()
    for cid, is_container, body in tree[0][2]:
        if cid == 0x1001 and not is_container:
            for mid, mbody in _walk_minichunks(body):
                if mid == _MC_N_FRAMES and len(mbody) >= 4:
                    anim.n_frames = struct.unpack_from("<I", mbody, 0)[0]
                elif mid == _MC_FPS and len(mbody) >= 4:
                    anim.fps = struct.unpack_from("<f", mbody, 0)[0]
                elif mid == _MC_N_ROT_WORDS and len(mbody) >= 4:
                    anim.n_rotation_words = struct.unpack_from("<I", mbody, 0)[0]
                elif mid == _MC_N_TRANS_WORDS and len(mbody) >= 4:
                    anim.n_translation_words = struct.unpack_from("<I", mbody, 0)[0]
        elif cid == 0x1002 and is_container:
            for cid2, ic2, body2 in body:
                if cid2 == 0x1003 and not ic2:
                    anim.bones.append(_parse_bone_info(body2))
        elif cid == 0x1009 and not is_container:
            anim.rotation_pool = body
        elif cid == 0x100a and not is_container:
            anim.translation_pool = body
    return anim


def unpack_quat(track: AlaBoneTrack, anim: AlaAnimation, frame: int) -> Tuple[float, float, float, float]:
    """Reconstruct the per-frame rotation quaternion (x, y, z, w) as
    floats in [-1, 1]. Falls back to the static default_rotation when
    the bone has no per-frame rotation track."""
    if track.idx_rotation < 0 or anim.n_rotation_words <= 0:
        return tuple(v / 32767.0 for v in track.default_rotation)
    if anim.n_frames == 0:
        return tuple(v / 32767.0 for v in track.default_rotation)
    if frame < 0 or frame >= anim.n_frames:
        raise IndexError(f"frame {frame} out of range [0, {anim.n_frames})")
    off = (frame * anim.n_rotation_words + track.idx_rotation) * 2
    if off + 8 > len(anim.rotation_pool):
        raise ValueError(f"rotation_pool too short to read bone at offset {off}")
    raw = struct.unpack_from("<4h", anim.rotation_pool, off)
    return tuple(v / 32767.0 for v in raw)


def unpack_translation(track: AlaBoneTrack, anim: AlaAnimation, frame: int) -> Tuple[float, float, float]:
    """Reconstruct the per-frame translation as floats in scene units.
    Falls back to the static trans_offset when the bone has no per-frame
    translation track. Mirrors `tools/ala_diff/main.cpp::unpack_trans`:
    pool stores uint16 values (memcpy'd through int16 in the writer);
    reconstruct as offset + scale * u16."""
    if track.idx_translation < 0 or anim.n_translation_words <= 0:
        return tuple(track.trans_offset)
    if anim.n_frames == 0:
        return tuple(track.trans_offset)
    if frame < 0 or frame >= anim.n_frames:
        raise IndexError(f"frame {frame} out of range [0, {anim.n_frames})")
    off = (frame * anim.n_translation_words + track.idx_translation) * 2
    if off + 6 > len(anim.translation_pool):
        raise ValueError(f"translation_pool too short to read bone at offset {off}")
    u = struct.unpack_from("<3H", anim.translation_pool, off)
    return (
        u[0] * track.trans_scale[0] + track.trans_offset[0],
        u[1] * track.trans_scale[1] + track.trans_offset[1],
        u[2] * track.trans_scale[2] + track.trans_offset[2],
    )

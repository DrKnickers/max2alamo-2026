"""validate_playback.py - Tier 4 automated playback-correctness checks.

Catches the regression class that ships through Tiers 1-3 unscathed:
writer produces structurally valid bytes (Tier 1), they round-trip
byte-identically (Tier 2), the feature-specific verifier passes
(Tier 3) -- yet the engine renders the fresh export wrong because
the on-disk values violate a vanilla-empirical convention the
engine relies on.

Three concrete bug classes this gate would have caught at fix time
(all shipped through to v0.9.1 release-prep before being noticed):

  Phase 14e (#87) -- `walk_animation` left `trans_offset` at the
    struct-zero default `(0, 0, 0)` for non-animatable bones (static-
    mesh attachment bones, light synth, proxy synth, light targets).
    Engine reads "bone is at parent-local (0,0,0) the moment any
    clip starts playing" and snaps attached rifles / lens flares to
    the parent's origin.  Vanilla content (e.g. MuzzleA_00 in
    EI_DARKTROOPER_ONE_ATTACK_00.ALA) always populates trans_offset
    from the bind matrix's translation; our walker now does too.

  Phase 14e (#87, rotation half) -- same loop also left
    `default_rotation` at the struct-zero identity quat
    `(0, 0, 0, 32767)` for non-animatable bones with non-identity
    bind rotation.  Engine reads "bone has no rotation" and
    discards the bone's natural orientation during animation.

  Phase 10.5 (#81) -- writer omitted the per-submesh `0x10006` chunk
    for shaders that need it (RSkin / B4I4 vertex formats).  The
    renderer reads per-vertex BoneIndices as global skeleton indices
    when 0x10006 is absent vs local-slot indices that need remap when
    present -- omitting 0x10006 made every skinned RSkin mesh render
    as stretched-triangle artifacts.

The checks are PROPERTY-based (vanilla-empirical conventions) rather
than baseline-pair comparisons.  Catches "writer left field X at
struct zero" or "writer emitted shader-X submesh without chunk Y"
regressions for every test in the harness, without per-test baseline
file upkeep.  See docs/build.md "Tier 4 - Playback validator
(automated)" for the user-facing summary.

Usage:
    python validate_playback.py <file.alo>

The script picks up sibling .ala files automatically:
  - <basename>.ala (single-clip back-compat)
  - <basename>_<CLIP>.ala (multi-clip; checked per clip)

Exit 0 = all checks pass.  Exit 1 = at least one assertion fails,
with per-failure detail printed to stderr.
"""
from __future__ import annotations

import glob
import os
import sys
from typing import List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo
import _ala

# Tolerances.  trans_offset comparison is in scene units; the loose
# rotation check is exact-equal against the struct-zero identity quat.
_TOL_TRANS = 1.0e-3


# ---- Check C (Phase 10.5): submeshes with RSkin/B4I4 vertex formats
#      must emit a 0x10006 bone-remap chunk -----------------------------

# These tokens in `vertex_format_name` mean "engine treats per-vertex
# BoneIndices as local slot indices, dereferenced via 0x10006 remap".
# Pulled from `alamo_format::vertex_format_selector`'s stock-shader
# table + AloViewer Models.cpp:155.  If the walker emits a format
# matching one of these but omits 0x10006, the renderer crashes the
# skinning pipeline.
_REMAP_FORMAT_TOKENS = ("RSkin", "B4I4")


def check_skin_bone_remap_presence(alo: _alo.Alo, errors: List[str]) -> None:
    for mesh_i, mesh in enumerate(alo.meshes):
        for sm_i, sm in enumerate(mesh.submeshes):
            fmt = sm.vertex_format_name or ""
            needs_remap = any(token in fmt for token in _REMAP_FORMAT_TOKENS)
            if needs_remap and sm.bone_remap is None:
                errors.append(
                    f"#C (Phase 10.5): mesh {mesh_i} submesh {sm_i} vertex_format "
                    f"{fmt!r} requires a 0x10006 bone-remap chunk but bone_remap "
                    f"is absent.  Walker probably skipped `apply_skin_bone_remap` "
                    f"for this submesh; check `scene_walker.cpp` skin-emit path."
                )


# ---- Check A + B (Phase 14e): non-animatable bones must inherit
#      bind values into the .ala static defaults ----------------------

_IDENTITY_QUAT_INT16_VARIANTS = (
    (0,  0,  0,  32767),
    (0,  0,  0, -32767),
)


def _bind_matrix_is_identity_rotation(matrix: List[float]) -> bool:
    """Bind matrix rotation is the 3x3 in column-major-by-element layout.
    Per encode_matrix3 (alamo_format/src/alo_build.cpp): matrix[0,4,8]
    is row0; matrix[1,5,9] is row1; matrix[2,6,10] is row2. Identity
    rotation has 1 on the diagonal (matrix[0], [5], [10]) and 0 elsewhere."""
    diag = (matrix[0], matrix[5], matrix[10])
    if not all(abs(d - 1.0) < 1.0e-3 for d in diag):
        return False
    off_diag = (matrix[1], matrix[2], matrix[4],
                matrix[6], matrix[8], matrix[9])
    return all(abs(o) < 1.0e-3 for o in off_diag)


def check_non_animatable_defaults(alo: _alo.Alo, ala: _ala.AlaAnimation,
                                  ala_label: str, errors: List[str]) -> None:
    """For every bone in the .ala that has neither rotation nor translation
    tracks (idx_rotation = idx_translation = -1), assert the static defaults
    inherit from the .alo bind matrix.

    Check A: trans_offset matches bind matrix translation row exactly
             (within _TOL_TRANS).
    Check B: if bind matrix has non-identity rotation, default_rotation
             must NOT be the struct-zero identity quat.  Loose form of
             the full quat-sign-aligned check; doesn't require a Max-
             equivalent matrix-to-quat impl in Python.  Catches the
             "walker forgot to set default_rotation" regression class.
    """
    for track in ala.bones:
        # Only care about NON-animatable bones (synth bones).  Animatable
        # bones have per-frame data so the static defaults are unused.
        if track.idx_translation >= 0 or track.idx_rotation >= 0:
            continue
        idx = track.skeleton_index
        if idx >= len(alo.bones):
            errors.append(
                f"#A/B ({ala_label}): track {track.name!r} skeleton_index={idx} "
                f"is out of bounds (skeleton has {len(alo.bones)} bones)"
            )
            continue

        alo_bone = alo.bones[idx]
        bind_trans = (alo_bone.matrix[3], alo_bone.matrix[7], alo_bone.matrix[11])

        # Check A: trans_offset must match bind translation.
        deltas = tuple(abs(track.trans_offset[i] - bind_trans[i]) for i in range(3))
        if max(deltas) > _TOL_TRANS:
            errors.append(
                f"#A (Phase 14e, {ala_label}): non-animatable bone {idx} "
                f"({alo_bone.name!r}) trans_offset={track.trans_offset} "
                f"diverges from bind matrix translation {bind_trans} "
                f"(max-delta {max(deltas):.5f} > {_TOL_TRANS}).  Engine "
                f"will render this bone at parent-local trans_offset, "
                f"not at the bind position.  Walker probably skipped "
                f"setting trans_offset for this synth bone in "
                f"`walk_animation` -- mirror the Phase 14e fix."
            )

        # Check B: if bind has non-identity rotation, default_rotation
        # can't be the struct-zero identity.
        bind_rot_is_identity = _bind_matrix_is_identity_rotation(alo_bone.matrix)
        default_is_zero_identity = (
            tuple(track.default_rotation) in _IDENTITY_QUAT_INT16_VARIANTS
        )
        if not bind_rot_is_identity and default_is_zero_identity:
            errors.append(
                f"#B (Phase 14e, {ala_label}): non-animatable bone {idx} "
                f"({alo_bone.name!r}) bind matrix has non-identity "
                f"rotation but default_rotation is the struct-zero "
                f"identity {track.default_rotation}.  Engine will "
                f"discard the bone's natural orientation during animation.  "
                f"Walker probably skipped extracting + packing the bind "
                f"rotation into default_rotation -- mirror the Phase 14e fix."
            )


# ---- driver --------------------------------------------------------------

def _find_companion_alas(alo_path: str) -> List[str]:
    """Locate .ala siblings: the single-clip <basename>.ala plus any
    multi-clip <basename>_<CLIP>.ala files."""
    base = alo_path[:-4] if alo_path.lower().endswith(".alo") else alo_path
    out = []
    single = base + ".ala"
    if os.path.exists(single):
        out.append(single)
    # Multi-clip: <base>_<CLIP>.ala (any clip name).  Walker convention
    # per Phase 11b.1.
    for path in sorted(glob.glob(base + "_*.ala")):
        out.append(path)
    return out


def main(argv: List[str]) -> int:
    if len(argv) < 2:
        print("usage: validate_playback.py <file.alo>", file=sys.stderr)
        return 2
    alo_path = argv[1]
    if not os.path.exists(alo_path):
        print(f"ERROR: {alo_path} does not exist", file=sys.stderr)
        return 2

    alo = _alo.load(alo_path)
    errors: List[str] = []

    # .alo-only check (Phase 10.5).
    check_skin_bone_remap_presence(alo, errors)

    # Cross-file checks (Phase 14e) -- run per .ala companion.
    companions = _find_companion_alas(alo_path)
    for ala_path in companions:
        ala = _ala.load(ala_path)
        ala_label = os.path.basename(ala_path)
        check_non_animatable_defaults(alo, ala, ala_label, errors)

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    n_meshes  = sum(len(m.submeshes) for m in alo.meshes)
    n_bones   = len(alo.bones)
    n_alas    = len(companions)
    print(f"OK  ({alo_path}: {n_bones} bones, {n_meshes} submeshes, "
          f"{n_alas} .ala companion(s) checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

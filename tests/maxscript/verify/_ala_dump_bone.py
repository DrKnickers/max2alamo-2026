"""_ala_dump_bone.py - diagnostic: dump a single bone's per-frame
local TM from a .ala. Verbose, human-readable output. Not a verifier;
use validate_playback.py for asserts.

Phase 14d / 14e diagnostic tool. Intent: verify per-frame data the
walker emits for a parent bone matches Max's view at the corresponding
frame, OR surface "track-absent" bones using static fallback values.

Usage:
    python _ala_dump_bone.py <file.ala> <bone_name> [frame_idx]

The parser + reconstruction math live in _ala.py. This script is
just a pretty-printer on top of that.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _ala


def main():
    if len(sys.argv) < 3:
        print("usage: _ala_dump_bone.py <file.ala> <bone_name> [frame_idx]")
        return 1
    path = sys.argv[1]
    target_name = sys.argv[2]
    frame_idx = int(sys.argv[3]) if len(sys.argv) >= 4 else 0

    anim = _ala.load(path)
    print(f"== {path} ==")
    print(f"  n_frames        = {anim.n_frames}")
    print(f"  n_rot_words     = {anim.n_rotation_words} "
          f"(=> rotation pool = {anim.n_rotation_words * anim.n_frames * 2} bytes; "
          f"have {len(anim.rotation_pool)})")
    print(f"  n_trans_words   = {anim.n_translation_words} "
          f"(=> translation pool = {anim.n_translation_words * anim.n_frames * 2} bytes; "
          f"have {len(anim.translation_pool)})")
    print(f"  bones count     = {len(anim.bones)}")

    target = next((t for t in anim.bones if t.name == target_name), None)
    if target is None:
        names = [t.name for t in anim.bones[:10]]
        print(f"ERROR: bone {target_name!r} not found. Available: {names} ...")
        return 1

    print(f"\n--- {target_name} track metadata ---")
    print(f"  idx_translation  = {target.idx_translation}")
    print(f"  idx_rotation     = {target.idx_rotation}")
    print(f"  trans_offset     = {target.trans_offset}")
    print(f"  trans_scale      = {target.trans_scale}")
    raw_dr = target.default_rotation
    unpacked_dr = tuple(v / 32767.0 for v in raw_dr)
    print(f"  default_rotation = {raw_dr} (raw int16) = {unpacked_dr} (unpacked quat)")

    print(f"\n--- frame {frame_idx} (of {anim.n_frames}) reconstruction ---")
    if target.idx_rotation >= 0 and anim.n_rotation_words > 0:
        q = _ala.unpack_quat(target, anim, frame_idx)
        print(f"  rotation quat    = ({q[0]:.5f}, {q[1]:.5f}, {q[2]:.5f}, {q[3]:.5f})")
    else:
        print(f"  rotation: NO TRACK (idx={target.idx_rotation}); "
              f"using default = {unpacked_dr}")

    if target.idx_translation >= 0 and anim.n_translation_words > 0:
        t = _ala.unpack_translation(target, anim, frame_idx)
        print(f"  translation real = ({t[0]:.5f}, {t[1]:.5f}, {t[2]:.5f})")
    else:
        t = target.trans_offset
        print(f"  translation: NO TRACK (idx={target.idx_translation}); "
              f"using trans_offset = ({t[0]:.5f}, {t[1]:.5f}, {t[2]:.5f})")

    return 0


if __name__ == "__main__":
    sys.exit(main())

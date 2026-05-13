-- test_translation_keyframes (Phase 8c):
-- Two real Max bones (BoneSys), one keyframed in position 0,0,0 -> 10,20,5
-- and the other keyframed in rotation 0deg -> 90deg about Z, over 30 frames.
-- The exporter samples IGameNode::GetLocalTM(t) once per (bone, frame) and
-- extracts both rotation (8b) and translation (8c) from the same Matrix3.
--
-- Per-bone translation packing (Phase 8c):
--   trans_offset = min position across the range
--   trans_scale  = (max - min) / 65535  (or 0 if no change)
--   packed[f]    = round((pos[f] - offset) / scale)  in [0, 65535] as uint16
-- Constant-axis edge case: trans_scale=0 -> packed values all 0; runtime
-- unpacks to offset (the constant). RotBone exercises this since its
-- position never changes.
--
-- Pinned by verify_test_translation_keyframes.py:
--   - n_frames = 31
--   - n_rotation_words = 8 (2 animatable bones * 4)
--   - n_translation_words = 6 (2 animatable bones * 3)
--   - n_scale_words = 0 (vanilla FoC has zero scale tracks)
--   - 0x100a (translation pool) emitted BEFORE 0x1009 (rotation pool)
--   - TransBone: idx_rot=0, idx_trans=0, trans_scale ~ (10/65535, 20/65535, 5/65535)
--   - RotBone:   idx_rot=4, idx_trans=3, trans_scale ~ (0,0,0)
--   - Frame[0] TransBone position ~ (0,0,0)
--   - Frame[30] TransBone position ~ (10,20,5)
--   - Frame[0] RotBone quat ~ identity (under the BoneSys static rotation)
--   - Frame[30] RotBone quat carries a 90deg Z rotation relative to frame 0

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 30

-- Two independent top-level bones. Created with explicit second-arg "to" so
-- BoneSys gives each one a deterministic initial orientation.
b_trans = BoneSys.createBone [0,  0, 0] [0, 20, 0] [0, 0, 1]
b_trans.name = "TransBone"

b_rot = BoneSys.createBone [0, 30, 0] [0, 50, 0] [0, 0, 1]
b_rot.name = "RotBone"

-- One static mesh so the .alo skeleton has a per-mesh attachment bone in
-- addition to the synthetic Root (vanilla shape; Mike's importer prefers it).
anchor_box = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

-- Keyframe both bones inside a single animate block. b_trans gets a
-- translation curve only; b_rot gets a rotation curve only. We also
-- explicitly hold b_rot's position at both endpoints so Max's BoneSys
-- doesn't compound rotation into the local TM's translation row (without
-- a position key, setting b.rotation on a BoneSys bone moves the bone
-- around the parent origin).
animate on
(
    at time 0  (b_trans.position = [0, 0, 0])
    at time 30 (b_trans.position = [10, 20, 5])

    at time 0  (b_rot.position = [0, 30, 0])
    at time 30 (b_rot.position = [0, 30, 0])

    at time 0  (b_rot.rotation = (eulerAngles 0 0 0))
    at time 30 (b_rot.rotation = (eulerAngles 0 0 90))
)

-- Diagnostic readback so a failed keyframe is obvious in the listener log.
trans_at_30 = at time 30 b_trans.position
rot_at_30   = at time 30 b_rot.rotation
format "  TransBone position @ frame 30 = % (expected [10,20,5])\n" trans_at_30
format "  RotBone rotation @ frame 30   = % (expected 90deg-Z applied)\n" rot_at_30

setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   30
setUserProp rootNode "Alamo_Anim_Name"  "trans_test"

format "  clip: Alamo_Anim_Start=0  Alamo_Anim_End=30  Alamo_Anim_Name=trans_test\n"

alamoTestExport "test_translation_keyframes"
quitMax #noPrompt

-- test_rotation_keyframes (Phase 8b):
-- Single Max bone keyframed 0deg -> 90deg about Z over 30 frames.
-- The exporter samples IGameNode::GetLocalTM(t) per frame for every
-- bone in bone_map, packs to int16 XYZW (XYZW order, scale 1/32767),
-- and emits a .ala next to the .alo with FoC framing + a 0x1009
-- rotation pool.
--
-- Clip metadata is authored on the scene-root node via three user
-- properties; the walker reads them in walk_animation:
--   Alamo_Anim_Start = 0
--   Alamo_Anim_End   = 30
--   Alamo_Anim_Name  = "rot_test"   (informational; not on disk yet)
--
-- Pinned (per verify_test_rotation_keyframes.py):
--   - .ala exists alongside the .alo
--   - n_frames = 31 (inclusive)
--   - FoC format (mini-chunks 11/12/13 present in 0x1001)
--   - n_rotation_words = 4 (one animated bone)
--   - n_translation_words = 0 (8b is rotation-only)
--   - TestBone has idx_rotation = 0, others have idx_rotation = -1
--   - Frame 0 quat ~ identity, Frame 30 quat ~ (0, 0, 0.7071, 0.7071)

fileIn "_harness.ms"
alamoTestSetup()

-- Set Max's scene animation range so the controllers evaluate over
-- [0, 30]. The exporter overrides this with the Alamo_Anim_* user
-- props, but the controllers themselves need to know the range.
animationRange = interval 0 30

-- One bone (real Max BoneSys bone, IGAME_BONE type). A non-Root mesh
-- is required for a vanilla-shaped skeleton (Mike's importer crashes
-- on Root-only files); a small Anchor cube fits the bill.
b = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
b.name = "TestBone"

anchorBox = Box length:5 width:5 height:5 pos:[10, 0, 0] name:"Anchor"

-- Keyframe a 90-degree rotation about world Z from frame 0 to 30.
-- (eulerAngles arg order is XYZ; rotating about Z is the 3rd
-- component.) MaxScript's `animate on` block captures these as
-- controller keys; without the block they'd just set the static pose.
animate on
(
    at time 0  (b.rotation = (eulerAngles 0 0 0))
    at time 30 (b.rotation = (eulerAngles 0 0 90))
)

-- Read back the controller value at frame 30 as a diagnostic so a
-- failed keyframe authoring is obvious in the listener log even if
-- the exporter sails through.
rot_at_30 = at time 30 b.rotation
format "  TestBone rotation @ frame 30 = % (expected something close to identity * 90deg Z)\n" rot_at_30

-- Author the clip metadata on the scene-root node. `rootNode` (the
-- global) returns the root of the Max scene graph.
setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   30
setUserProp rootNode "Alamo_Anim_Name"  "rot_test"

format "  clip: Alamo_Anim_Start=0  Alamo_Anim_End=30  Alamo_Anim_Name=rot_test\n"

alamoTestExport "test_rotation_keyframes"
quitMax #noPrompt

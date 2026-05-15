-- test_phase14b_static_mesh_parent_bone (Phase 14b):
-- Static (non-skinned) Box parented to a BoneSys bone in Max. The
-- walker must encode the box's synthetic per-mesh attachment bone
-- with parent_index pointing at the HandBone (so the box follows
-- animation), NOT at scene Root. Regression target: EI_SNOWTROOPER's
-- gun, which used to float free of the hand during playback (PR #86,
-- issue #84 follow-up).
--
-- Predicted bytes (asserted by verify_test_phase14b_static_mesh_parent_bone.py):
--   bones[0] = "Root"      parent=0xFFFFFFFF
--   bones[1] = "HandBone"  parent=0
--   bones[2] = "GunMesh"   parent=1                       <- the fix
--                          translation row ~ (200, 0, 0)  <- parent-local, not world

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 10

-- HandBone: pivot at world (500, 0, 0), display along world +Y.
hand = BoneSys.createBone [500, 0, 0] [500, 1000, 0] [0, 0, 1]
hand.name = "HandBone"

-- GunMesh: a plain Box, placed in world such that its parent-local
-- translation relative to HandBone comes out as (200, 0, 0) -- 200
-- units along HandBone's local +X axis (= world +Y at bind pose).
-- HandBone is at world (500, 0, 0) with rotation that aligns local
-- +X with world +Y, so 200 along local +X = world (0, 200, 0)
-- offset from the hand, i.e. world (500, 200, 0).
gun = Box length:50 width:50 height:50 pos:[500, 200, 0] name:"GunMesh"
gun.parent = hand

-- Animate the hand so the human visual check in AloViewer can
-- confirm the gun follows. The byte-level test does NOT depend on
-- the animation values.
animate on (
    at time 0  (hand.rotation = (eulerAngles 0 0 0))
    at time 10 (hand.rotation = (eulerAngles 0 0 45))
)
setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   10
setUserProp rootNode "Alamo_Anim_Name"  "ph14b"

format "  phase 14b: static box parented to bone\n"
alamoTestExport "test_phase14b_static_mesh_parent_bone"
quitMax #noPrompt

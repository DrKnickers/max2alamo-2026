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
--                          translation row ~ (500, 0, 0)  <- bind position
--   bones[2] = "GunMesh"   parent=1                       <- the fix
--                          translation row ~ (0, 200, 0)  <- parent-local, not world

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 10

-- HandBone: pivot at world (500, 0, 0), bone display direction +X
-- (createBone with from-to along +X leaves node TM rotation at
-- identity). Earlier versions of this fixture used
-- `createBone [500,0,0] [500,1000,0]` (direction +Y), which bakes
-- +90 Z into the bone's node TM -- and the `at time 0 (hand.rotation
-- = identity)` keyframe below then orbits the bone's translation
-- around the local pivot to (0, -500, 0), silently corrupting the
-- bind pose. See Phase 14d (#87) for the diagnostic that surfaced
-- this. The fix here is to keep node TM rotation at identity so
-- the keyframe is a no-op for translation.
hand = BoneSys.createBone [500, 0, 0] [510, 0, 0] [0, 0, 1]
hand.name = "HandBone"

-- GunMesh: a plain Box at world (500, 200, 0), parented to
-- HandBone. With HandBone at identity rotation + T(500, 0, 0),
-- GunMesh's parent-local translation is (0, 200, 0) -- the
-- world delta with no rotation transformation.
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

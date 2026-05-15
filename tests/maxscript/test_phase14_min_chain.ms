-- test_phase14_min_chain (Phase 14 diagnostic):
-- Minimal two-bone parent-child chain with a single Z-axis rotation
-- keyframed on the child. Designed as the smallest viable reproducer
-- for issue #84 (animation playback pose mismatch).
--
-- The hope: even a 2-bone chain triggers the "wrong pose" symptom
-- AloViewer shows on EI_SNOWTROOPER. If it does, the bug is in the
-- per-frame extraction convention (compose_with_object_offset, quat
-- packing basis, or parent-relative encoding) -- NOT in something
-- specific to 36-bone biped rigs. From a minimal scene we can bisect
-- the convention by toggling one variable at a time.
--
-- Predicted bytes (for `ala_diff --dump` after export):
--   n_frames           = 11
--   fps                = 30
--   n_bones            = 3            (Root synthetic + ParentBone + ChildBone)
--   is_foc             = true
--   n_rotation_words   = 8            (4 per animatable bone; Root is synthetic)
--   n_translation_words= 6            (3 per animatable bone)
--
--   ParentBone:
--     rotation track: identity for every frame
--                     -> packed [0, 0, 0, 32767] x 11
--     translation track: (0, 0, 0) for every frame
--   ChildBone:
--     rotation track: interpolates identity -> 90deg-about-Z over 11 frames
--                     frame 0:  identity        = (0, 0, 0, 1)
--                     frame 5:  45deg about Z   = (0, 0, sin22.5, cos22.5)
--                                                = (0, 0, 0.3827, 0.9239)
--                     frame 10: 90deg about Z   = (0, 0, sin45, cos45)
--                                                = (0, 0, 0.7071, 0.7071)
--     translation track: (0, 20, 0) for every frame (child's parent-local pivot)
--
-- After Max-side export, run:
--   build/tools/ala_diff/Release/ala_diff.exe --dump \
--     build/maxbatch/test_phase14_min_chain.ala
--
-- Then load .alo + .ala in AloViewer and observe frames 0 / 5 / 10.
-- Decision tree is in docs/plans/phase-14-diagnostic.md.

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 10

-- Two-bone chain along +Y. ParentBone runs from origin to [0, 20, 0];
-- ChildBone hangs off the parent's end, running from [0, 20, 0] to
-- [0, 40, 0]. BoneSys.createBone places the bone with its pivot at
-- the start point and its end at the second point.
bParent = BoneSys.createBone [0,  0, 0] [0, 20, 0] [0, 0, 1]
bParent.name = "ParentBone"
bChild  = BoneSys.createBone [0, 20, 0] [0, 40, 0] [0, 0, 1]
bChild.name = "ChildBone"
bChild.parent = bParent

-- Tiny skinned cylinder along the child bone, so AloViewer's mesh
-- visualization reflects the bone pose even without "show skeleton".
cyl = Cylinder radius:1.5 height:20 heightsegs:2 sides:6 \
               pos:[0, 20, 0] name:"ChildMesh"
-- Orient the cylinder along +Y so its long axis follows the child bone.
rotate cyl (eulerAngles 90 0 0)
modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk
skinOps.addBone sk bChild 0

snap = snapshotAsMesh cyl
for v = 1 to snap.numverts do (
    skinOps.SetVertexWeights sk v #(1) #(1.0)
)

-- Anchor non-skinned box so the export contains a static mesh too.
anchorBox = Box length:5 width:5 height:5 pos:[40, 0, 0] name:"AnchorBox"

-- Keyframe the child: identity at frame 0, 90deg-about-Z at frame 10.
-- BoneSys rotation accepts eulerAngles in (X, Y, Z) order.
animate on (
    at time 0  (bChild.rotation = (eulerAngles 0 0 0))
    at time 10 (bChild.rotation = (eulerAngles 0 0 90))
)

setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   10
setUserProp rootNode "Alamo_Anim_Name"  "min_chain"

format "  phase 14 min chain: parent (static) + child (90deg Z over 11 frames)\n"
alamoTestExport "test_phase14_min_chain"
quitMax #noPrompt

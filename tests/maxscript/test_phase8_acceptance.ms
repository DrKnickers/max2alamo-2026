-- test_phase8_acceptance (Phase 8e):
-- Full animation-surface integration test. Mirrors Phase 7d's static
-- acceptance scene (11 bones / 2 meshes / 2 lights / 2 proxies) and
-- overlays animation: rotation tracks (8b), translation tracks (8c),
-- visibility tracks (8d), and a pivot-orientation interaction (5g x 8b)
-- on a Dummy helper. The point is cross-feature coverage: per-feature
-- tests pin each animation surface in isolation; this one proves they
-- compose without leaking tracks across bone categories or breaking
-- pre-existing static-surface invariants.
--
-- Scene composition (11 bones, 2 meshes, 2 lights, 2 proxies):
--   Bones (bone_map):
--     BoneA            (real, root-level)         rotation 0->90 Z (8b)
--     BoneB            (real, root-level sibling) translation D=(5,10,15) (8c)
--     HelperBone       (Dummy + 5e, root-level)   rotation 0->45 X (8b on helper)
--     PivotedHardpoint (Dummy + 5e + objectoffsetrot, root-level)
--                                                 rotation 0->60 Z + static -90 Y offset
--                                                 (5g x 8b interaction)
--   Bones (visibility_map only, NOT bone_map):
--     StaticBox        per-mesh attachment bone   visibility T->F (8d on static-mesh)
--     OmniLight        per-light synth bone       visibility T->F (8d on light)
--     SpotLight        per-light synth bone       NO animation (control)
--     SpotLight.Target sibling bone               NO animation (control)
--     prox0            proxy synth bone           visibility F->T (8d asymmetric)
--     prox1            proxy synth bone           NO animation (control)
--   Meshes:
--     SkinCyl          (skinned to BoneA+BoneB, no per-mesh bone)
--     StaticBox        (static, attachment-bone)
--
-- Expected: 4 bones in bone_map (BoneA, BoneB, HelperBone, PivotedHardpoint)
-- yielding n_rotation_words = 16, n_translation_words = 12, n_scale_words = 0.
-- 11 bones in visibility_map; 3 emit 0x1007 (StaticBox, OmniLight, prox0)
-- and 7 are elided (the 4 bone_map members plus SpotLight + Target + prox1).
--
-- BoneA and BoneB are root-level siblings (not parent-child) so the
-- translation track is sampled in BoneB's own local frame without
-- BoneA's rotation contaminating per-frame readings.
--
-- The verifier (verify_test_phase8_acceptance.py) checks ~62 invariants
-- across 10 groups; see its docstring.

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 30

-- ============================================================
-- Bones (bone_map members: real + helpers-as-bones)
-- ============================================================

bA = BoneSys.createBone [0,  0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "BoneA"

bB = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bB.name = "BoneB"
-- BoneB authored at world origin (matches test_translation_keyframes
-- recipe). NOT bB.parent = bA -- root-level siblings to keep BoneB's
-- local TM clean of BoneA's per-frame rotation. BoneSys.createBone with
-- non-zero start coords stores the TM in a way that complicates the
-- frame[0] - frame[30] delta; starting at origin sidesteps that.

helperBone = Dummy boxsize:[5,5,5] pos:[30, 0, 0] name:"HelperBone"
setUserProp helperBone "Alamo_Export_Transform" true

-- PivotedHardpoint: 5g object-offset rotation (-90 about Y) baked in
-- statically, plus per-frame node rotation 0->60 around Z. On disk,
-- each frame's bone matrix is compose(offsetrot, node_rot).
-- PivotedHardpoint at world origin so per-frame rotation doesn't
-- translate it (Max's Dummy-with-objectoffsetrot causes rotation around
-- the object's pivot, which moves the world position of a non-origin-
-- centered helper). Origin-centered keeps trans_scale == 0 and lets the
-- verifier pin the 5g x 8b composition on rotation alone.
pivotedHP = Dummy boxsize:[5,5,5] pos:[0, 0, 0] name:"PivotedHardpoint"
setUserProp pivotedHP "Alamo_Export_Transform" true
pivotedHP.objectoffsetrot = quat 0 -0.70710678 0 0.70710678

-- ============================================================
-- Lights
-- ============================================================

ol = Omnilight name:"OmniLight" pos:[100, 0, 30]
ol.rgb = color 255 255 255
ol.multiplier = 1.0

sl = TargetSpot name:"SpotLight" pos:[0, 100, 30] target:(Targetobject pos:[0, 0, 0])
sl.rgb = color 255 255 255
sl.multiplier = 1.0
sl.hotspot = 30.0
sl.falloff = 45.0

-- ============================================================
-- Proxies
-- ============================================================

prox0 = Alamo_Proxy name:"prox0" pos:[30, 30, 0]
prox1 = Alamo_Proxy name:"prox1" pos:[40, 30, 0]

-- ============================================================
-- Static mesh (gets its own synth attachment bone in visibility_map)
-- ============================================================

sb = Box length:10 width:10 height:10 pos:[60, 0, 5] name:"StaticBox"

-- ============================================================
-- Skinned cylinder bound to BoneA + BoneB rigidly. Same recipe as
-- test_phase7_acceptance: snapshotAsMesh to work around skinOps quirk
-- in 3dsmaxbatch, per-vertex linear blend across the joint band.
-- ============================================================

cyl = Cylinder radius:3 height:40 heightsegs:6 sides:8 \
               pos:[0, -25, 0] name:"SkinCyl"
rotate cyl (eulerAngles -90 0 0)

modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk

skinOps.addBone sk bA 0
skinOps.addBone sk bB 0

snap = snapshotAsMesh cyl
nVerts = snap.numverts
for v = 1 to nVerts do (
    p = getVert snap v
    -- Linear blend across y in [-25, +15], pure B0 -> pure B1.
    alpha = (p.y + 25.0) / 40.0
    if alpha < 0.0 then alpha = 0.0
    if alpha > 1.0 then alpha = 1.0
    w0 = 1.0 - alpha
    w1 = alpha
    skinOps.SetVertexWeights sk v #(1, 2) #(w0, w1)
)
format "  SkinCyl: % verts bound to BoneA + BoneB\n" nVerts

-- ============================================================
-- Animation overlay (single animate-on block; all keys at once)
-- ============================================================

animate on (
    -- BoneA: 0 -> 90 around Z (8b)
    at time 0  (bA.rotation = (eulerAngles 0 0 0))
    at time 30 (bA.rotation = (eulerAngles 0 0 90))
    -- Hold BoneA's position so BoneSys doesn't move it during rotation.
    at time 0  (bA.position = [0, 0, 0])
    at time 30 (bA.position = [0, 0, 0])

    -- BoneB: position [0, 0, 0] -> [5, 10, 15] (delta = (5, 10, 15), 8c)
    at time 0  (bB.position = [0, 0, 0])
    at time 30 (bB.position = [5, 10, 15])
    -- Hold rotation so it doesn't compound.
    at time 0  (bB.rotation = (eulerAngles 0 0 0))
    at time 30 (bB.rotation = (eulerAngles 0 0 0))

    -- HelperBone: 0 -> 45 around X (8b on helper-as-bone)
    at time 0  (helperBone.rotation = (eulerAngles 0 0 0))
    at time 30 (helperBone.rotation = (eulerAngles 45 0 0))

    -- PivotedHardpoint: 0 -> 60 around Z on top of static -90 Y offset.
    -- Authored at world origin so rotation doesn't translate the node.
    at time 0  (pivotedHP.rotation = (eulerAngles 0 0 0))
    at time 30 (pivotedHP.rotation = (eulerAngles 0 0 60))

    -- OmniLight: visible -> hidden (8d on light synth bone)
    at time 0  (ol.visibility = true)
    at time 30 (ol.visibility = false)

    -- StaticBox: visible -> hidden (8d on static-mesh attachment bone)
    at time 0  (sb.visibility = true)
    at time 30 (sb.visibility = false)

    -- prox0: hidden -> visible (8d, asymmetric direction)
    at time 0  (prox0.visibility = false)
    at time 30 (prox0.visibility = true)
)

-- Diagnostic readback (visible in listener log).
format "  BoneA @ frame 30 = %\n" (at time 30 bA.rotation)
format "  BoneB @ frame 30 = %\n" (at time 30 bB.position)
format "  HelperBone @ frame 30 = %\n" (at time 30 helperBone.rotation)
format "  PivotedHardpoint @ frame 0  rot = % pos = %\n" \
    (at time 0 pivotedHP.rotation) (at time 0 pivotedHP.position)
format "  PivotedHardpoint @ frame 30 rot = % pos = %\n" \
    (at time 30 pivotedHP.rotation) (at time 30 pivotedHP.position)
format "  PivotedHardpoint offset = %\n" pivotedHP.objectoffsetrot
format "  OmniLight visibility @ 0/30 = % / %\n" \
    (at time 0 ol.visibility) (at time 30 ol.visibility)
format "  StaticBox visibility @ 0/30 = % / %\n" \
    (at time 0 sb.visibility) (at time 30 sb.visibility)
format "  prox0 visibility @ 0/30 = % / %\n" \
    (at time 0 prox0.visibility) (at time 30 prox0.visibility)
format "  SpotLight visibility @ 0/30 = % / % (expect constant true)\n" \
    (at time 0 sl.visibility) (at time 30 sl.visibility)
format "  prox1 visibility @ 0/30 = % / % (expect constant true)\n" \
    (at time 0 prox1.visibility) (at time 30 prox1.visibility)

setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   30
setUserProp rootNode "Alamo_Anim_Name"  "phase8_acceptance"

format "  clip: start=0 end=30 name=phase8_acceptance\n"
format "  scene: 4 anim'd bones + 3 anim'd visibility tracks + 3 control bones (no spurious tracks)\n"

alamoTestExport "test_phase8_acceptance"
quitMax #noPrompt

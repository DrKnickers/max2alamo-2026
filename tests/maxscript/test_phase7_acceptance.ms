-- test_phase7_acceptance (Phase 7d):
-- Full-surface integration test. A single scene combines every Phase
-- 4..7 feature that's been shipped so far -- skinned mesh, real bones,
-- a helper-as-bone, a static mesh, an Omni light, a TargetSpot with
-- its sibling .Target bone, and two Alamo_Proxy helpers (one default,
-- one Alamo_Geometry_Hidden). The point is interaction coverage:
-- per-feature tests already pin each surface in isolation; this one
-- proves they hold together when the walker sees all of them in one
-- pass.
--
-- Scene composition (11 bones, 2 meshes, 2 lights, 2 proxies):
--   Bones:     B0, B1                                  (real, skin chain)
--              ExportedPivot                           (helper-as-bone, 5e)
--   Meshes:    SkinCyl                                 (skinned to B0+B1)
--              StaticBox                               (static, attachment-bone)
--   Lights:    OmniMain                                (type 0)
--              SpotMain (+ SpotMain.Target sibling)    (type 2)
--   Proxies:   p_alpha   (defaults)                    (mini 5+6 only)
--              p_beta    (Alamo_Geometry_Hidden=true)  (+ mini 7)
--
-- Expected on-disk skeleton: Root, B0, B1, ExportedPivot, StaticBox,
-- OmniMain, SpotMain, SpotMain.Target, p_alpha, p_beta -- 11 total.
-- Skinned SkinCyl connects to Root (no per-mesh bone); StaticBox gets
-- its own attachment bone. Connections = 4 (2 meshes + 2 lights);
-- proxies are NOT connection objects.
--
-- The skin paint block is lifted verbatim from
-- test_smooth_skinned_joint.ms (Phase 5c); the bone-chain shape and
-- snapshotAsMesh workaround for the 3dsmaxbatch
-- skinOps.GetNumberVertices==0 quirk are documented there.

fileIn "_harness.ms"
alamoTestSetup()

-- 2-bone chain along world +Y; joint at Y=40.
b0 = BoneSys.createBone [0,  0, 0] [0, 40, 0] [0, 0, 1]; b0.name = "B0"
b1 = BoneSys.createBone [0, 40, 0] [0, 80, 0] [0, 0, 1]; b1.name = "B1"
b1.parent = b0

-- Helper-as-bone (Phase 5e surface).
expivot = Dummy boxsize:[5,5,5] pos:[15, 25, 35] name:"ExportedPivot"
setUserProp expivot "Alamo_Export_Transform" true

-- Static mesh at an offset (Phase 4 + attachment-bone surface).
sb = Box length:10 width:10 height:10 pos:[50, 50, 0] name:"StaticBox"

-- Omni light (Phase 7b.1 surface).
ol = Omnilight name:"OmniMain" pos:[100, 0, 30]
ol.rgb = color 51 102 204    -- distinctive (~0.2, 0.4, 0.8)
ol.multiplier = 1.5
ol.useFarAtten = true
ol.farAttenStart = 50.0
ol.farAttenEnd = 300.0

-- TargetSpot + sibling .Target bone (Phase 7b.2 surface).
-- Construct with a placeholder target then leave it at the origin so
-- the verifier can check SpotMain.Target's translation against (0,0,0).
sl = TargetSpot name:"SpotMain" pos:[0, 100, 30] target:(Targetobject pos:[0, 0, 0])
sl.rgb = color 255 255 255
sl.multiplier = 1.0
sl.hotspot = 30.0   -- degrees (MaxScript surface) -> radians on disk
sl.falloff = 45.0

-- Two Alamo_Proxy helpers exercising the with/without flag paths.
pa = Alamo_Proxy name:"p_alpha" pos:[30, 30, 0]
pb = Alamo_Proxy name:"p_beta"  pos:[40, 30, 0]
setUserProp pb "Alamo_Geometry_Hidden" true

-- Skinned cylinder along the 2-bone chain. Same recipe as
-- test_smooth_skinned_joint: 80-unit cylinder rotated -90 about X so
-- its local height axis (+Z) aligns with world +Y, with weights linear-
-- blended across the 40-unit transition band centered on the joint.
cyl = Cylinder radius:5 height:80 heightsegs:8 sides:8 \
               pos:[0, 0, 0] name:"SkinCyl"
rotate cyl (eulerAngles -90 0 0)

modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk

skinOps.addBone sk b0 0
skinOps.addBone sk b1 0

-- See test_smooth_skinned_joint.ms for why we paint via snapshotAsMesh
-- rather than skinOps.GetNumberVertices (which returns 0 in batch mode).
snap = snapshotAsMesh cyl
nVerts = snap.numverts
format "  SkinCyl mesh has % vertices (skinOps reports %; using mesh count)\n" \
       nVerts (skinOps.GetNumberVertices sk)
joint50 = 0
purelyB0 = 0
purelyB1 = 0
mixed = 0
for v = 1 to nVerts do
(
    local p = getVert snap v
    local alpha = (p.y - 20.0) / 40.0
    if alpha < 0.0 then alpha = 0.0
    if alpha > 1.0 then alpha = 1.0
    local w0 = 1.0 - alpha
    local w1 = alpha
    skinOps.SetVertexWeights sk v #(1, 2) #(w0, w1)
    if w0 == 1.0 then purelyB0 += 1
    else if w1 == 1.0 then purelyB1 += 1
    else if w0 == 0.5 then joint50 += 1
    else mixed += 1
)
format "  weights painted: pure-B0=%, joint-50/50=%, mixed=%, pure-B1=%\n" \
       purelyB0 joint50 mixed purelyB1

format "  scene: 2 real bones + helper-as-bone + 2 meshes + 2 lights + 2 proxies\n"

alamoTestExport "test_phase7_acceptance"
quitMax #noPrompt

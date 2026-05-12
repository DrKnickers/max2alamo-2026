-- test_smooth_skinned_joint (Phase 5c):
-- 2-bone chain with a cylinder spanning the joint. Weights are
-- smooth-painted as a linear blend across a 40-unit transition band
-- centered on the joint, so vertices on the joint plane split 50/50
-- between B0 and B1 and intermediate vertices carry mixed influences --
-- exercising the top-4 / renormalize path even with only 2 bones.
--
-- Bones along world +Y, cylinder rotated -90 about X so its native
-- height axis (local +Z) aligns with world +Y. This matches the
-- working test_skinned_cylinder pattern; rotating around X (or any
-- non-cylinder-symmetric axis) is what actually flushes the procedural
-- mesh evaluation -- without that, skinOps.GetNumberVertices returns 0.
--
-- Key alignment: a vertex at world-Y=h sits at object-Z=h in the
-- mesh's local frame (the -90 X rotation maps local +Z to world +Y).
-- So we paint weights using world Y, the walker writes object-space
-- positions with height in Z, and the verifier checks position.z ==
-- world-Y values. They're literally the same numbers, just relabelled.
--
-- Pinned behaviour:
--   - 3 bones: synthetic Root + B0 + B1; NO per-mesh attachment bone
--   - cylinder connection -> bone#0 (Root) (skinned-mesh convention)
--   - every vertex's weights[0..3] sum to ~1.0
--   - at least one vertex carries influences from BOTH bones with
--     non-zero weight in slots 0 AND 1 (multi-bone, not just single)
--   - joint-row verts at Z=40 split ~0.5/0.5 between B0 and B1
--   - tip verts at Z=0 are bound rigidly to B0 (weight 1.0)
--   - tip verts at Z=80 are bound rigidly to B1 (weight 1.0)

fileIn "_harness.ms"
alamoTestSetup()

-- 2-bone chain along world +Y; joint at Y=40.
b0 = BoneSys.createBone [0,  0, 0] [0, 40, 0] [0, 0, 1]; b0.name = "B0"
b1 = BoneSys.createBone [0, 40, 0] [0, 80, 0] [0, 0, 1]; b1.name = "B1"
b1.parent = b0

-- Cylinder 80 units tall, 8 height segments -> 9 vertex rings at
-- height = 0, 10, 20, 30, 40, 50, 60, 70, 80.
cyl = Cylinder radius:5 height:80 heightsegs:8 sides:8 \
               pos:[0, 0, 0] name:"SmoothCyl"
-- -90 about X aligns the cylinder's height axis with world +Y AND
-- forces Max to materialize the procedural mesh (a Z-axis no-op
-- rotation gets optimised away, leaving skinOps with 0 vertices).
rotate cyl (eulerAngles -90 0 0)

modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk

skinOps.addBone sk b0 0
skinOps.addBone sk b1 0

-- Smooth blend: w_B1 = clamp((Y - 20) / 40, 0, 1); w_B0 = 1 - w_B1
--   Y <= 20  -> (1.0, 0.0)   pure B0
--   Y == 30  -> (0.75, 0.25)
--   Y == 40  -> (0.5, 0.5)   joint, 50/50 split
--   Y == 50  -> (0.25, 0.75)
--   Y >= 60  -> (0.0, 1.0)   pure B1
--
-- 3dsmaxbatch quirk: skinOps.GetNumberVertices returns 0 here even
-- when Skin is fully attached (the modifier-panel UI state needed to
-- populate that count isn't entered in batch mode). Drive the loop
-- from the underlying mesh snapshot's vertex count instead -- and
-- use the snapshot's per-vert positions so we don't depend on
-- skinOps.GetVertexPosition either. skinOps.SetVertexWeights itself
-- does accept explicit vertex indices even when GetNumberVertices
-- lies, so the paint takes effect.
snap = snapshotAsMesh cyl
nVerts = snap.numverts
format "  mesh has % vertices (skinOps reports %; using mesh count)\n" \
       nVerts (skinOps.GetNumberVertices sk)
joint50 = 0
purelyB0 = 0
purelyB1 = 0
mixed = 0
for v = 1 to nVerts do
(
    -- snapshotAsMesh bakes the node's transform into the returned
    -- mesh, so getVert here returns WORLD-space positions. With the
    -- -90 X rotation, world Y is the cylinder's height axis, matching
    -- the object-space Z that the walker writes into the .alo (since
    -- gmesh->GetVertex with ObjectSpace=true returns the pre-rotation
    -- frame where height is along local +Z). World Y at paint time =
    -- object-space Z at write time = vert.position[2] in the verifier.
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

alamoTestExport "test_smooth_skinned_joint"
quitMax #noPrompt

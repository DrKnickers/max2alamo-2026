-- test_pivot_skinned_safety (Phase 5g, regression guard):
-- 2-bone chain where B0 has a non-identity objectoffsetrot and B1 doesn't.
-- A cylinder is skinned across both bones with rigid (1.0-weight) per-row
-- binding. The fix composes objectoffsetrot into bone matrices, which
-- could in principle break skinned-mesh vertex deformation if Skin
-- modifier weight-binding assumed node-TM-only matrices.
--
-- This test exists to catch any regression where skinning visibly drifts.
-- It uses the simplest possible skin config (rigid per-row) so the
-- verifier has known expected values.
--
-- Pinned (per verify_test_pivot_skinned_safety.py):
--   - B0 bone matrix carries the offset (col != identity)
--   - B1 bone matrix is ~identity (no offset)
--   - SkinCyl mesh has 1 submesh; vertex count matches a cylinder
--     with heightsegs=2, sides=8 (3 rings * 8 + 2 caps = 32 verts)
--   - All vertex weights sum to 1.0 within 1e-3
--   - Every vertex's bone_indices[0] resolves to a valid bone

fileIn "_harness.ms"
alamoTestSetup()

-- 2-bone chain along +Y. B0 at [0,0,0]->[0,40,0]; B1 at [0,40,0]->[0,80,0].
b0 = BoneSys.createBone [0,  0, 0] [0, 40, 0] [0, 0, 1]; b0.name = "B0_Pivoted"
b1 = BoneSys.createBone [0, 40, 0] [0, 80, 0] [0, 0, 1]; b1.name = "B1"
b1.parent = b0

-- B0 gets a pivot-only rotation (the Phase 5g fix should capture this).
-- B1 is left alone (identity offset).
b0.objectoffsetrot = (quat 0 0 -0.7071068 0.7071068)

-- Cylinder along world +Y (rotated -90 about X aligns local Z with world Y,
-- matching the skinning-test pattern from Phase 5b/5c).
cyl = Cylinder radius:5 height:80 heightsegs:2 sides:8 \
               pos:[0, 0, 0] name:"SkinCyl"
rotate cyl (eulerAngles -90 0 0)

modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk
skinOps.addBone sk b0 0
skinOps.addBone sk b1 0

-- Rigid binding: bottom half to B0, top half to B1. Use the same
-- snapshotAsMesh trick as test_smooth_skinned_joint to bypass the
-- 3dsmaxbatch skinOps.GetNumberVertices=0 quirk.
snap = snapshotAsMesh cyl
nVerts = snap.numverts
for v = 1 to nVerts do
(
    local p = getVert snap v
    if p.y <= 40.0 then skinOps.SetVertexWeights sk v #(1, 2) #(1.0, 0.0)
    else skinOps.SetVertexWeights sk v #(1, 2) #(0.0, 1.0)
)

format "  2-bone chain (B0 pivoted -90deg-Z, B1 identity) + skinned cylinder\n"

alamoTestExport "test_pivot_skinned_safety"
quitMax #noPrompt

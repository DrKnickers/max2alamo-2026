-- test_skinned_cylinder (Phase 5b):
-- 3-bone chain along world +Y with a cylinder skinned to those bones.
-- The Skin modifier is configured so different vertex rows along the
-- cylinder rigid-attach to different bones (no smooth multi-bone
-- weighting; that's Phase 5c).
--
-- Pinned behaviour:
--   - 4 bones in scene.bones: synthetic Root + B0 + B1 + B2
--   - NO synthetic per-mesh attachment bone for the cylinder
--   - cylinder's connection -> bone#0 (Root), matching vanilla skinned
--     content (AI_DACTILLION.ALO)
--   - per-vertex bone_indices[0] points into {1, 2, 3} (the chain
--     bones) with weight 1.0; never 0 (Root) for any vertex
--   - some verts bind to B0, some to B1, some to B2 (distribution)

fileIn "_harness.ms"
alamoTestSetup()

-- 3-bone chain along world +Y, lengths matched to cylinder segments
-- below so the skin assignment is unambiguous per row.
b0 = BoneSys.createBone [0,  0, 0] [0, 20, 0] [0, 0, 1]; b0.name = "B0"
b1 = BoneSys.createBone [0, 20, 0] [0, 40, 0] [0, 0, 1]; b1.name = "B1"
b1.parent = b0
b2 = BoneSys.createBone [0, 40, 0] [0, 60, 0] [0, 0, 1]; b2.name = "B2"
b2.parent = b1

-- Cylinder along Y, 60 units long, 4 height segments (-> 5 vertex rows
-- at Y = 0, 15, 30, 45, 60). Each row will be assigned rigidly to one
-- bone.
cyl = Cylinder radius:5 height:60 heightsegs:4 sides:8 \
               pos:[0, 0, 0] name:"SkinnedCyl"
-- Rotate so the cylinder's height axis matches the bone chain (+Y world).
-- Max cylinders are created with height along +Z by default.
rotate cyl (eulerAngles -90 0 0)

-- Apply Skin modifier and add the 3 bones. Skin in MAXScript:
modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk

skinOps.addBone sk b0 0
skinOps.addBone sk b1 0
skinOps.addBone sk b2 0

-- Assign each cylinder vertex to its nearest bone. Cylinder has 8
-- sides + 1 cap each end (top + bottom). Iterate vertices, look at the
-- world Y, pick a bone:
--   Y in [-inf, 20)  -> B0  (skin bone slot 1, since skin bone slots are 1-based)
--   Y in [20,   40)  -> B1
--   Y in [40,  inf)  -> B2
-- The skin modifier API uses 1-based bone-id within the skin's bone list.
nVerts = skinOps.GetNumberVertices sk
format "  cylinder has % skinnable vertices\n" nVerts
for v = 1 to nVerts do
(
    local p = skinOps.GetVertexPosition sk v   -- world space
    local boneIdx = 1
    if p.y >= 40.0 then boneIdx = 3
    else if p.y >= 20.0 then boneIdx = 2
    skinOps.ReplaceVertexWeights sk v boneIdx 1.0
)

format "  rigidly assigned: Y<20 -> B0, 20<=Y<40 -> B1, Y>=40 -> B2\n"

alamoTestExport "test_skinned_cylinder"
quitMax #noPrompt

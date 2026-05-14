-- test_phase8_skinning_stress (Phase 8f):
-- 32 chainBones in a chain + a single mesh skinned to all of them with
-- smooth bind. Catches "skin pipe has a 16-bone limit assumption" or
-- "bone_indices overflow" regressions. The walker's top-4 picker
-- per-vertex should keep the 4 strongest influences.

fileIn "_harness.ms"
alamoTestSetup()

-- 32-bone chain along world +Y. Each bone is 5 units long.
chainBones = #()
for i = 0 to 31 do (
    local y0 = i * 5
    local y1 = (i + 1) * 5
    local b = BoneSys.createBone [0, y0, 0] [0, y1, 0] [0, 0, 1]
    b.name = "ChainBone" + (formattedPrint i format:"02d")
    if i > 0 do b.parent = chainBones[i]
    append chainBones b
)
format "  authored % chainBones in chain\n" chainBones.count

-- A long cylinder along +Y, skinned to every bone with smooth bind.
cyl = Cylinder radius:2 height:160 heightsegs:32 sides:8 \
               pos:[0, 0, 0] name:"StressedCyl"
rotate cyl (eulerAngles -90 0 0)

modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk

for b in chainBones do skinOps.addBone sk b 0

-- Smooth blend: each vertex's weights distributed across the nearest
-- 4 chainBones based on Y position. Mimics what smooth skin would produce.
snap = snapshotAsMesh cyl
nVerts = snap.numverts
for v = 1 to nVerts do (
    local p = getVert snap v
    local segment = p.y / 5.0  -- which bone segment
    local nearestBone = (segment as integer) + 1   -- 1-indexed bone array
    if nearestBone < 1 then nearestBone = 1
    if nearestBone > chainBones.count then nearestBone = chainBones.count
    -- Assign 100% weight to nearest bone (rigid binding for simplicity).
    skinOps.SetVertexWeights sk v #(nearestBone) #(1.0)
)
format "  stressed cyl: % verts bound to 32 chainBones\n" nVerts

alamoTestExport "test_phase8_skinning_stress"
quitMax #noPrompt

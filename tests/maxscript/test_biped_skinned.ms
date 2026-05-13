-- test_biped_skinned (5e empirical verification):
-- Default Max Biped + cylinder skinned to two of its bones. Confirms
-- that Max's Biped sub-bones reach the walker as IGAME_BONE (rather
-- than as some Biped-specific type) and round-trip into the .alo
-- skeleton with the standard 0x202/0x203/0x206 chunks.
--
-- Pinned behaviour:
--   - Skeleton contains Root (synthetic) + every Biped sub-node by
--     name. A complete default biped has 20+ bones.
--   - The skinned mesh "BipSkinned" connects to bone#0 (Root) per the
--     5b skinned-mesh convention.
--   - At least one cylinder vertex's slot-0 bone_index points at a
--     biped bone (IGameSkin -> walker -> bone_map path works through
--     the biped chain).

fileIn "_harness.ms"
alamoTestSetup()

-- Create a default biped of height 100. createBiped picks reasonable
-- proportions for spine/limbs; we just need its skeleton structure.
bipRoot = biped.createNew 100 0 [0, 0, 0]
format "  biped created; root node: %\n" bipRoot.name

-- Discover biped sub-nodes by walking the hierarchy (don't assume
-- a fixed name prefix: Max 2026 numbers the first biped Bip001, but
-- subsequent ones or other locales may differ).
fn collectBiped node out =
(
    append out node
    for c in node.children do collectBiped c out
)
bipedNodes = #()
collectBiped bipRoot bipedNodes
format "  biped has % nodes total\n" bipedNodes.count

-- Pick a spine bone and a head bone by name fragment so we skin to
-- two distinct biped sub-nodes regardless of numbering scheme.
spineNode = undefined
headNode  = undefined
for n in bipedNodes do (
    if spineNode == undefined and (findString n.name "Spine") != undefined then spineNode = n
    if headNode  == undefined and (findString n.name "Head")  != undefined then headNode  = n
)
format "  spine pick: %\n" (if spineNode == undefined then "<none>" else spineNode.name)
format "  head pick:  %\n" (if headNode  == undefined then "<none>" else headNode.name)

-- Skinned cylinder near the biped's upper body.
cyl = Cylinder radius:3 height:50 heightsegs:4 sides:8 \
               pos:[0, 0, 50] name:"BipSkinned"
rotate cyl (eulerAngles -90 0 0)   -- force evaluation (existing harness pattern)

modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk

-- Only skin to bones we actually found. (skinOps.addBone on undefined
-- crashes the export pipeline downstream, which is what bit the first
-- iteration of this test.)
if spineNode != undefined then skinOps.addBone sk spineNode 0
if headNode  != undefined then skinOps.addBone sk headNode  0

format "  cylinder skinned to biped Spine + Head bones\n"

alamoTestExport "test_biped_skinned"
quitMax #noPrompt

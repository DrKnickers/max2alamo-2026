-- test_bone_hierarchy (Phase 5a):
-- A 3-bone chain plus a small static mesh. Verifies that real Max
-- bones make it into the export with:
--   - canonical hierarchy ordering (parent comes before child in
--     scene.bones)
--   - parent_index pointing at the immediate parent bone (not Root)
--   - local-to-parent matrix translation (child Y offset, NOT world Y)
--   - still co-existing with the static mesh's per-mesh attachment
--     bone (Phase 4c behavior unchanged for unskinned meshes)

fileIn "_harness.ms"
alamoTestSetup()

-- 3-bone chain along Y. Each bone is placed 25 units further from its
-- parent, so local-to-parent translation should be (0, 25, 0) for the
-- two non-root chain bones.
bRoot = BoneSys.createBone [0, 0, 0]  [0, 25, 0] [0, 0, 1]
bRoot.name = "B_Root"

bMid = BoneSys.createBone [0, 25, 0] [0, 50, 0] [0, 0, 1]
bMid.name = "B_Mid"
bMid.parent = bRoot

bTip = BoneSys.createBone [0, 50, 0] [0, 75, 0] [0, 0, 1]
bTip.name = "B_Tip"
bTip.parent = bMid

format "  3 bones: B_Root@(0,0,0), B_Mid@(0,25,0), B_Tip@(0,50,0)\n"

-- Static mesh alongside. Its per-mesh attachment bone gets appended
-- AFTER the real bones, so its index in scene.bones is 4 (Root=0,
-- B_Root=1, B_Mid=2, B_Tip=3, BoxA=4).
b = Box length:10 width:10 height:10 pos:[20, 0, 0] name:"BoxA"
format "  static mesh BoxA at (20, 0, 0)\n"

alamoTestExport "test_bone_hierarchy"
quitMax #noPrompt

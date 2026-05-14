-- test_phase8_geometry_edge_cases (Phase 8f):
-- Corner cases that exist in real authoring:
--   NoMaterialMesh -- a Box without a material assigned.
--   MultiMaterialMesh -- a Box with a Multi/Sub-Object material (2 sub-materials).
-- Catches "walker crashes on missing material" or "submesh dispatch
-- broken under Multi/Sub" regressions.

fileIn "_harness.ms"
alamoTestSetup()

-- A box with NO material assigned.
noMatBox = Box length:10 width:10 height:10 pos:[0, 0, 0] name:"NoMaterialMesh"
-- (mat = undefined by default for new boxes)

-- A box with a Multi/Sub-Object material -- 2 materials, face mat-IDs
-- assigned per-half. (Default Box gives faces mat-ID 1..6 for the 6 sides;
-- we leave defaults to exercise multi-submesh dispatch.)
multiMat = Multimaterial numsubs:2
multiMat.materialList[1] = StandardMaterial name:"SubMat1" diffuseColor:(color 255 0 0)
multiMat.materialList[2] = StandardMaterial name:"SubMat2" diffuseColor:(color 0 0 255)
multiBox = Box length:10 width:10 height:10 pos:[20, 0, 0] name:"MultiMaterialMesh"
multiBox.material = multiMat

format "  NoMaterialMesh + MultiMaterialMesh (with 2 sub-materials)\n"
alamoTestExport "test_phase8_geometry_edge_cases"
quitMax #noPrompt

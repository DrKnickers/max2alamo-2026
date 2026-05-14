-- test_legacy_mc80_d (Phase 10b):
-- MC80 destroyed-state capital ship. Audit baseline: 77 nodes including
-- 62 Missing_Helper substitutions (the legacy plugin's helper class
-- [0x23b708db, 0x00000100] -- Phase 10d will register a stub to
-- resolve these to Alamo_Proxy instances; for 10b they still substitute
-- and export as helper-as-bones via Alamo_Export_Transform user prop).
-- 12 Editable_Poly meshes (damage chunks). 3 DXMaterials. No
-- Alamo_Anim_* -> no .ala.

fileIn "_harness.ms"
alamoTestSetup()

fixturePath = alamoRepoRoot + "/tests/corpus/legacy/ThrREv/Ascendancy/MC80/RV_MonCalCruiser_D_export4.max"
if not (doesFileExist fixturePath) then (
    format "[harness] FAIL: legacy fixture missing at %\n" fixturePath
    quitMax #noPrompt
)
loadMaxFile fixturePath quiet:true useFileUnits:true
format "[legacy] loaded % -> % nodes\n" fixturePath objects.count
alamoTestExport "test_legacy_mc80_d"
quitMax #noPrompt

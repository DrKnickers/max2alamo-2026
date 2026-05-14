-- test_legacy_snowtrooper (Phase 10b):
-- Legacy Snowtrooper infantry rig from ThrREv Ascendancy. Audit baseline:
-- 41 nodes (32 BoneGeometry + 3 helpers + 3 IK_Chain), 5 DXMaterials with
-- mixed shader paths (some Max 9 install, some IcW mod). Export should
-- produce 36 bones / 6 meshes (3 skinned). No Alamo_Anim_* -> no .ala.

fileIn "_harness.ms"
alamoTestSetup()

fixturePath = alamoRepoRoot + "/tests/corpus/legacy/ThrREv/Ascendancy/Snowtrooper/EI_SNOWTROOPER.max"
if not (doesFileExist fixturePath) then (
    format "[harness] FAIL: legacy fixture missing at %\n" fixturePath
    quitMax #noPrompt
)
loadMaxFile fixturePath quiet:true useFileUnits:true
format "[legacy] loaded % -> % nodes\n" fixturePath objects.count
alamoTestExport "test_legacy_snowtrooper"
quitMax #noPrompt

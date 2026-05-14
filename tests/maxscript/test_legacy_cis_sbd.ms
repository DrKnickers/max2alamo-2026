-- test_legacy_cis_sbd (Phase 10b):
-- Legacy max2alamo.dle compatibility regression. Opens an unmodified
-- Thrawn's Revenge Ascendancy infantry rig (Super Battle Droid)
-- authored in Max 9 era, exports through our Max 2026 plugin, and
-- verifies the export still produces a clean .alo.
--
-- Audit baseline (Phase 10a):
--   - 30 scene nodes: 19 BoneGeometry + 7 helpers (Alamo_Export_Transform=true) +
--     1 Dummy + 2 PolyMeshObject + 1 Editable_Poly + 1 Box + 4 IK_Chain_Object
--   - 4 DXMaterials (paths broken locally; walker matches by basename)
--   - No Alamo_Anim_* clip metadata -> should NOT emit .ala (Phase 10b walker fix)
--   - Phase 10a's export produced 25 bones, 4 meshes, 2 skinned -> 1.5 MB .alo

fileIn "_harness.ms"
alamoTestSetup()

fixturePath = alamoRepoRoot + "/tests/corpus/legacy/ThrREv/Ascendancy/Super Battle Droid/CIS_SBD.max"
if not (doesFileExist fixturePath) then (
    format "[harness] FAIL: legacy fixture missing at %\n" fixturePath
    format "[harness] one-time setup: copy from ThrREv mod-tools to tests/corpus/legacy/\n"
    format "[harness] (see docs/corpus.md)\n"
    quitMax #noPrompt
)

loaded = loadMaxFile fixturePath quiet:true useFileUnits:true
format "[legacy] loaded % -> %, % nodes\n" fixturePath loaded objects.count

alamoTestExport "test_legacy_cis_sbd"
quitMax #noPrompt

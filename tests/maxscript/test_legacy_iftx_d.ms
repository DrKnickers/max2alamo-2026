-- test_legacy_iftx_d (Phase 10b):
-- IFTX ground vehicle destroyed-state. Audit baseline: 5 nodes (3
-- Missing_Helper + 2 Editable_Poly), 1 DXMaterial. Smallest of the
-- destroyed-variant fixtures; smoke-tests the Missing_Helper -> helper-
-- as-bone fallback path on a minimal scene.

fileIn "_harness.ms"
alamoTestSetup()

fixturePath = alamoRepoRoot + "/tests/corpus/legacy/ThrREv/Ascendancy/IFTX/EV_IFTX_D.max"
if not (doesFileExist fixturePath) then (
    format "[harness] FAIL: legacy fixture missing at %\n" fixturePath
    quitMax #noPrompt
)
loadMaxFile fixturePath quiet:true useFileUnits:true
format "[legacy] loaded % -> % nodes\n" fixturePath objects.count
alamoTestExport "test_legacy_iftx_d"
quitMax #noPrompt

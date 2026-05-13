-- _harness.ms — shared helpers for tests/maxscript/test_*.ms.
--
-- A test script `fileIn`s this, calls `alamoTestSetup()` to reset the
-- scene, builds whatever geometry/materials it needs, then calls
-- `alamoTestExport "<test_name>"` to write the .alo to a deterministic
-- path under build/maxbatch/<test_name>.alo. The runner pairs that
-- output with tests/maxscript/verify/verify_<test_name>.py.
--
-- Run a single test by hand:
--   "C:\Program Files\Autodesk\3ds Max 2026\3dsmaxbatch.exe" \
--     tests\maxscript\test_static_box.ms \
--     -listenerlog build\maxbatch\test_static_box.log -v 2
--
-- Run the whole suite:
--   pwsh scripts/run-max-tests.ps1

-- Derive the repo root from this script's own location so the harness
-- works from any worktree (the main checkout AND .claude/worktrees/...).
-- getSourceFileName() returns the full path to the .ms currently being
-- executed; we climb three levels (_harness.ms -> maxscript -> tests ->
-- <repo>). If the call fails for any reason (e.g. running from the
-- listener or a future Max version), fall back to the legacy hardcoded
-- path so the existing behavior is preserved.
fn alamoComputeRepoRoot =
(
    local me = undefined
    try ( me = getSourceFileName() ) catch ( me = undefined )
    if me == undefined or me == "" then
        return "C:/Modding/max2alamo-2026"
    local d = pathConfig.removePathLeaf me        -- <repo>/tests/maxscript
    d = pathConfig.removePathLeaf d               -- <repo>/tests
    d = pathConfig.removePathLeaf d               -- <repo>
    substituteString d "\\" "/"
)

global alamoRepoRoot = alamoComputeRepoRoot()
format "[harness] repo root resolved to: %\n" alamoRepoRoot

fn alamoTestSetup =
(
    resetMaxFile #noPrompt
    units.SystemType = #Inches
    format "[harness] scene reset\n"
)

fn alamoTestExport testName =
(
    local outDir  = alamoRepoRoot + "/build/maxbatch"
    makeDir outDir all:true
    local outPath = outDir + "/" + testName + ".alo"
    try ( deleteFile outPath ) catch ()
    try ( deleteFile (outPath + ".export.log") ) catch ()
    format "[harness] exporting -> %\n" outPath
    local ok = exportFile outPath #noPrompt selectedOnly:false using:Alamo_Object
    format "[harness] exportFile returned: %\n" ok
    if not (doesFileExist outPath) then
    (
        format "[harness] ERROR: % was not created\n" outPath
        false
    )
    else
    (
        format "[harness] OK: % bytes\n" (getFileSize outPath)
        true
    )
)

fn alamoFxPath shaderFile =
(
    alamoRepoRoot + "/shaders/max-preview/" + shaderFile
)

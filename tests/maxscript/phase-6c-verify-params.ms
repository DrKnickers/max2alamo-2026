-- phase-6c-verify-params.ms
--
-- Walker-side verification for the per-material parameter extraction:
-- build a scene with a DirectX Shader material whose Specular and
-- Shininess parameters have been tweaked away from the .fx defaults,
-- export via max2alamo, and write the output + the plugin's .export.log
-- to a known path under build/maxbatch/. The accompanying Python decoder
-- (scripts/dump-tangents.py et al) then inspects the on-disk material
-- chunks and confirms the tweaks survived through:
--   IDxMaterial::ParamBlock(0)  -> extract_pblock_params
--   -> ExportMaterial::params   -> build_submesh_material
--   -> 0x10106 / 0x10103 chunks
-- end-to-end.
--
-- Run via:
--     "C:\Program Files\Autodesk\3ds Max 2026\3dsmaxbatch.exe" \
--        tests/maxscript/phase-6c-verify-params.ms \
--        -listenerlog build/maxbatch/phase-6c-verify-params.log -v 2

format "=== phase-6c-verify-params.ms ===\n"

-- ----- Scene setup ---------------------------------------------------------

resetMaxFile #noPrompt
units.SystemType = #Inches  -- match what default Max scenes use

-- Box at world (10, 0, 5) so the bone TM bake is non-trivial.
b = Box length:30 width:40 height:20 pos:[10, 0, 5] name:"TestBox"

-- Sphere at world (-15, 20, 5) — second mesh tests multi-mesh + per-mesh bone.
s = GeoSphere radius:12 pos:[-15, 20, 5] name:"TestSphere"

-- ----- DirectX Shader material with MeshBumpColorize.fx stub ---------------

repoRoot = "C:/Modding/max2alamo-2026"
fxPath   = repoRoot + "/shaders/max-preview/MeshBumpColorize.fx"

mat = DirectX_9_Shader name:"TestMat"
-- effectFile is a property on the material; setting it triggers Max to
-- parse the .fx and surface the params in the paramblock.
mat.effectFile = fxPath
format "  effectFile = %\n" mat.effectFile

-- Tweak Specular + Shininess to non-default values. These are the values
-- the decoder will assert against. If they survive to the bytes we know
-- the walker -> writer path is correct.
--
-- The DirectX Shader material exposes effect parameters as properties
-- with names matching the .fx declarations. Try both setProperty and
-- direct dotted access -- catch any failure.
fn setMaterialParam mat n value =
(
    local ok = true
    try ( setProperty mat n value ) catch
    (
        ok = false
        format "    setProperty %=% FAILED: %\n" n value (getCurrentException())
    )
    ok
)

setMaterialParam mat "Specular"     (color  25  25  25)    -- ~ (0.10, 0.10, 0.10)
setMaterialParam mat "Shininess"    8.0
setMaterialParam mat "Diffuse"      (color 200 180 160)    -- ~ (0.78, 0.71, 0.63)
setMaterialParam mat "Emissive"     (color   0   0   0)
setMaterialParam mat "Colorization" (color   0 255   0)    -- pure green

b.material = mat
s.material = mat

-- ----- Export --------------------------------------------------------------

outDir  = repoRoot + "/build/maxbatch"
makeDir outDir all:true
outPath = outDir + "/phase-6c-verify-params.alo"
try ( deleteFile outPath ) catch ()
try ( deleteFile (outPath + ".export.log") ) catch ()

format "  exporting to %\n" outPath
exportRet = exportFile outPath #noPrompt selectedOnly:false using:Alamo_Object
format "  exportFile returned: %\n" exportRet

if (doesFileExist outPath) then
    format "  OK: % bytes\n" (getFileSize outPath)
else
    format "  ERROR: output file not created\n"

format "=== done ===\n"
quitMax #noPrompt

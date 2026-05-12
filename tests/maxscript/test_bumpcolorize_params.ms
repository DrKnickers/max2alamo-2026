-- test_bumpcolorize_params: regression for PR #20 (per-material param
-- extraction from DXMaterial ParamBlock) and PR #22 (float3 alpha=0).
--
-- A scene with a DirectX Shader material whose Specular/Shininess/
-- Diffuse/Colorization values are set away from the .fx defaults. The
-- verifier asserts that those tweaks land in the on-disk material
-- chunks AND that float3-declared params (Emissive/Diffuse/Specular)
-- write 0 in the 4th slot while genuine float4 params (Colorization)
-- preserve theirs.

fileIn "_harness.ms"
alamoTestSetup()

b = Box length:30 width:40 height:20 pos:[10, 0, 5] name:"TestBox"
s = GeoSphere radius:12 pos:[-15, 20, 5] name:"TestSphere"

mat = DirectX_9_Shader name:"TestMat"
mat.effectFile = alamoFxPath "MeshBumpColorize.fx"

-- Non-default values that the verifier asserts against. Max color is
-- 0..255 per channel; the engine reads 0..1.
setProperty mat "Specular"     (color  25  25  25)    -- ~ (0.098,)*3
setProperty mat "Shininess"    8.0
setProperty mat "Diffuse"      (color 200 180 160)    -- ~ (0.784, 0.706, 0.627)
setProperty mat "Emissive"     (color   0   0   0)
setProperty mat "Colorization" (color   0 255   0)    -- pure green

b.material = mat
s.material = mat

alamoTestExport "test_bumpcolorize_params"
quitMax #noPrompt

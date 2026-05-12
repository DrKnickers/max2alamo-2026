-- test_skinned_rskin: integration check — skinned mesh + RSkin shader.
--
-- Stresses the full pipeline by exercising every Phase 5/6 path at
-- once: real bone hierarchy (5a), single-bone skinning via IGameSkin
-- (5b), DirectX Shader material extraction (6c) with non-default
-- parameter values, float3 alpha=0 convention (6c fix), and the
-- coordinate-frame banner (6d) all on a single mesh.
--
-- Authors a 3-bone chain + cylinder + Skin modifier (same shape as
-- test_skinned_cylinder), but instead of a Standard fallback this
-- one wires up a DirectX_9_Shader pointing at our RSkinBumpColorize.fx
-- stub with tuned Specular / Shininess / Colorization.
--
-- The verifier confirms that:
--   - 4 bones (Root + B0/B1/B2), no synthetic per-mesh attachment bone
--   - connection -> bone#0 (Root)
--   - shader_name = "RSkinBumpColorize.fx" + 6 material params in
--     canonical order with override values surviving
--   - Specular / Diffuse / Emissive have alpha=0 (float3 convention)
--   - Colorization preserves alpha=1 (genuine float4)
--   - per-vertex bone_indices[0] still resolves to chain bones (1..3)

fileIn "_harness.ms"
alamoTestSetup()

-- 3-bone chain along world +Y, 20 units between joints.
b0 = BoneSys.createBone [0,  0, 0] [0, 20, 0] [0, 0, 1]; b0.name = "B0"
b1 = BoneSys.createBone [0, 20, 0] [0, 40, 0] [0, 0, 1]; b1.name = "B1"
b1.parent = b0
b2 = BoneSys.createBone [0, 40, 0] [0, 60, 0] [0, 0, 1]; b2.name = "B2"
b2.parent = b1

-- Cylinder along Y, 60 units long.
cyl = Cylinder radius:5 height:60 heightsegs:4 sides:8 \
               pos:[0, 0, 0] name:"RSkinCyl"
rotate cyl (eulerAngles -90 0 0)

-- RSkin DirectX shader. Modders use this for animated skinned content;
-- the .fx parameter set matches MeshBumpColorize so the same param
-- extraction code path should fire.
mat = DirectX_9_Shader name:"RSkinMat"
mat.effectFile = alamoFxPath "RSkinBumpColorize.fx"

-- Non-default values that the verifier asserts against. Same values
-- used in test_bumpcolorize_params so any divergence pins to the
-- Mesh-vs-RSkin difference, not to the skin-vs-static difference.
setProperty mat "Specular"     (color  25  25  25)    -- ~ (0.098,)*3
setProperty mat "Shininess"    8.0
setProperty mat "Diffuse"      (color 200 180 160)    -- ~ (0.784, 0.706, 0.627)
setProperty mat "Emissive"     (color   0   0   0)
setProperty mat "Colorization" (color   0 255   0)    -- pure green

cyl.material = mat

-- Skin modifier rigged the same way as test_skinned_cylinder.
modPanel.setCurrentObject cyl.baseObject
sk = Skin()
addModifier cyl sk
modPanel.setCurrentObject sk
skinOps.addBone sk b0 0
skinOps.addBone sk b1 0
skinOps.addBone sk b2 0

nVerts = skinOps.GetNumberVertices sk
for v = 1 to nVerts do
(
    local p = skinOps.GetVertexPosition sk v
    local boneIdx = 1
    if p.y >= 40.0 then boneIdx = 3
    else if p.y >= 20.0 then boneIdx = 2
    skinOps.ReplaceVertexWeights sk v boneIdx 1.0
)

format "  RSkin material applied, skin assignments by Y row\n"
alamoTestExport "test_skinned_rskin"
quitMax #noPrompt

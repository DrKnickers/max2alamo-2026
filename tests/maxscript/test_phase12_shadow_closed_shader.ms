-- test_phase12_shadow_closed_shader (Phase 12):
-- A closed (manifold) Box with material shader_name = MeshShadowVolume.fx.
-- The validator detects the shader-name trigger and runs the closed-
-- volume check; the cube is closed, so NO warning is emitted.
--
-- Pinned behaviour:
--   - .export.log contains NO "WARNING: shadow-volume" line
--   - the cube exports normally (mesh present, name = ClosedShadowBox)

fileIn "_harness.ms"
alamoTestSetup()

b = Box name:"ClosedShadowBox" width:10 length:10 height:10 pos:[0,0,0]
m = standardMaterial name:"ShadowMat" diffuseColor:(color 200 200 200)
b.material = m
setUserProp b "Alamo_Shader_Name" "MeshShadowVolume.fx"

format "  ClosedShadowBox: 6-face cube, shader=MeshShadowVolume.fx\n"

alamoTestExport "test_phase12_shadow_closed_shader"
quitMax #noPrompt

-- test_phase12_shadow_open_shader (Phase 12):
-- An OPEN box (top polygon deleted) with shader = MeshShadowVolume.fx.
-- The validator must detect the open boundary and emit exactly one
-- warning line per offending mesh to .export.log.
--
-- Construction: start with a Box primitive, convert to Editable Poly,
-- delete the top polygon (4 boundary edges remain). The cube's 5 quad
-- faces split into 10 triangles; the 4 missing-top edges each lose one
-- incidence -> non_manifold_edge_count = 4.
--
-- Pinned behaviour:
--   - .export.log contains exactly one "WARNING: shadow-volume mesh
--     'OpenShadowBox' has 4 non-manifold edge(s)" line
--   - export still succeeds (warn, not abort)

fileIn "_harness.ms"
alamoTestSetup()

b = Box name:"OpenShadowBox" width:10 length:10 height:10 pos:[0,0,0]
convertToPoly b

-- The default Box has 6 quad faces, one per side. Pick the +Z (top)
-- face by normal direction. NOTE: MaxScript `local` only works inside
-- blocks (memorised quirk); at top level use plain assignment.
topFaceIdx = -1
for f = 1 to (polyOp.getNumFaces b) do
(
    local n = polyOp.getFaceNormal b f
    if n.z > 0.9 then
    (
        topFaceIdx = f
        exit
    )
)
if topFaceIdx == -1 then
(
    format "[test] ERROR: could not locate top face by normal\n"
)
else
(
    polyOp.deleteFaces b #(topFaceIdx)
    format "  OpenShadowBox: deleted top face at index %\n" topFaceIdx
)

setUserProp b "Alamo_Shader_Name" "MeshShadowVolume.fx"
format "  OpenShadowBox: shader=MeshShadowVolume.fx, 5 faces (top removed)\n"

alamoTestExport "test_phase12_shadow_open_shader"
quitMax #noPrompt

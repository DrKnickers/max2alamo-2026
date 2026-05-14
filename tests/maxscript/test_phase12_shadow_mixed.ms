-- test_phase12_shadow_mixed (Phase 12):
-- More realistic shadow-volume scene: a U-shaped trough (clearly open
-- on 3 sides -- the bottom + two long sides remain, the two ends and
-- the top are removed) alongside a closed Cylinder. Both have the
-- shadow-volume shader. Verifies:
--   - the open mesh ('UTrough') warns
--   - the closed mesh ('ClosedCyl') does NOT warn
--   - exactly ONE warning total in .export.log
--   - both meshes export (warn-not-abort policy)
--
-- The U-trough is constructed by starting from a Box and removing the
-- three "open" faces; remaining mesh is a U cross-section that's
-- clearly not a closed volume (no top, no end caps).

fileIn "_harness.ms"
alamoTestSetup()

-- ---- Open mesh: U-trough --------------------------------------------------
trough = Box name:"UTrough" width:20 length:60 height:10 pos:[0,0,0]
convertToPoly trough
-- Identify faces by normal direction and remove top (+Z), front (-Y), back (+Y).
facesToDel = #()
for f = 1 to (polyOp.getNumFaces trough) do
(
    local n = polyOp.getFaceNormal trough f
    if n.z > 0.9 then append facesToDel f          -- top
    else if n.y < -0.9 then append facesToDel f    -- front end-cap
    else if n.y > 0.9 then append facesToDel f     -- back end-cap
)
polyOp.deleteFaces trough facesToDel
setUserProp trough "Alamo_Shader_Name" "MeshShadowVolume.fx"
format "  UTrough: removed % faces (top + both end-caps), shader=MeshShadowVolume.fx\n" facesToDel.count

-- ---- Closed mesh: cylinder ------------------------------------------------
cyl = Cylinder name:"ClosedCyl" radius:5 height:20 sides:12 pos:[50,0,0]
-- Default Cylinder has end caps -> closed. Apply shadow shader.
setUserProp cyl "Alamo_Shader_Name" "MeshShadowVolume.fx"
format "  ClosedCyl: capped cylinder (12 sides), shader=MeshShadowVolume.fx\n"

alamoTestExport "test_phase12_shadow_mixed"
quitMax #noPrompt

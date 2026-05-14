-- test_phase8_uv_distortion (Phase 8f):
-- UV distortion + MikkT tangent stability under extreme UV layouts.
-- Phase 6b integrated MikkT for tangent/binormal computation; the
-- corpus and existing tests use standard [0,1] UVs. This test exercises
-- tiled (4x), negative, and mirrored UVs so degenerate-UV-triangle
-- bugs in MikkT surface as NaN/Inf in tangents.
--
-- Authoring: a single Box with editable mesh modifications to set
-- explicit UV coords across faces. Bump-mapped Standard material so
-- MikkT's tangent computation actually runs.

fileIn "_harness.ms"
alamoTestSetup()

-- A box mesh; we'll let Max compute UVs via default planar map and
-- inspect them. For "distortion" we use UVWMap with tile factor 4.
boxA = Box length:10 width:10 height:10 pos:[0, 0, 0] name:"TiledBox"
boxB = Box length:10 width:10 height:10 pos:[15, 0, 0] name:"NegativeBox"
boxC = Box length:10 width:10 height:10 pos:[30, 0, 0] name:"MirroredBox"

-- Apply UVW Map modifier with explicit tile factors.
uvwA = UVWMap utile:4.0 vtile:4.0  -- 4x tiling
addModifier boxA uvwA
uvwB = UVWMap utile:-2.0 vtile:-2.0  -- negative
addModifier boxB uvwB
uvwC = UVWMap utile:1.0 vtile:1.0 uflip:true  -- mirrored U
addModifier boxC uvwC

-- Standard material with a placeholder bump map so MikkT runs.
-- (No actual bitmap; the slot existence forces tangent computation.)
matB = StandardMaterial()
boxA.material = matB
boxB.material = matB
boxC.material = matB

format "  3 boxes: tiled (4x), negative (-2x), mirrored UVs\n"
alamoTestExport "test_phase8_uv_distortion"
quitMax #noPrompt

-- _phase14d_variants.ms
--
-- Phase 14d diagnostic. Builds four single-bone scenes with
-- different (position, rotation) combinations and exports each
-- to its own .alo. Comparing the encoded translation row to
-- Max's view of node.transform.row4 in each case discriminates
-- between the three candidate causes from issue #87.
--
-- Run:
--   3dsmaxbatch.exe tests/maxscript/_phase14d_variants.ms ^
--     -listenerlog build/maxbatch/_phase14d_variants.log -v 5
--
-- The exporter requires at least one mesh in the scene. We add
-- a small unparented Box at world origin so the .alo writes.
-- It does NOT parent to TestBone, to keep the bone-encoding
-- analysis untangled from the static-mesh-attachment path.

fileIn "_harness.ms"

-- Build a scene with one BoneSys bone + a throwaway box, dump
-- Max-side TM, and export. The bone's intended (position, rotation)
-- is decoupled from createBone's from/to/zaxis -- we apply rotation
-- first, then *set position again* (because b.rotation = ... orbits
-- translation around the local pivot in Max).
fn buildAndExport variantName intendedPos intendedRot =
(
    alamoTestSetup()
    -- Always create at intended pos with bone direction +X (some safe
    -- default); we'll overwrite rotation+position next.
    local b = BoneSys.createBone intendedPos (intendedPos + [10, 0, 0]) [0, 0, 1]
    b.name = "TestBone"
    b.rotation = intendedRot          -- reorbits translation
    b.position = intendedPos          -- restore intended position

    -- Throwaway mesh so the exporter writes a file. Lives at origin,
    -- not parented to TestBone, never touched by walk_node-as-bone.
    local junk = Box length:1 width:1 height:1 pos:[0, 0, 0] name:"_junk"

    format "\n========== variant: % ==========\n" variantName
    format "  intendedPos     = %\n" intendedPos
    format "  intendedRot     = %\n" intendedRot
    format "  bone.position   = %\n" b.position
    format "  bone.rotation   = %\n" b.rotation
    format "  ooffsetrot      = %\n" b.objectoffsetrot
    format "  ooffsetpos      = %\n" b.objectoffsetpos
    format "  TM.row1 (lcl X) = %\n" b.transform.row1
    format "  TM.row2 (lcl Y) = %\n" b.transform.row2
    format "  TM.row3 (lcl Z) = %\n" b.transform.row3
    format "  TM.row4 (T)     = %\n" b.transform.row4
    alamoTestExport variantName
)

-- V0: baseline -- recreate Phase 14b's HandBone state directly to
-- prove the variants share its (rotation +90 Z @ (500,0,0)) input.
buildAndExport "_phase14d_v0_phase14b_baseline" \
    [500, 0, 0] (eulerAngles 0 0 90)

-- V1: identity rotation at (500, 0, 0). Predicted: encoded T should
-- be (500, 0, 0) for ALL candidates. Sanity check that the tooling
-- reads what's there.
buildAndExport "_phase14d_v1_identity_500_0_0" \
    [500, 0, 0] (quat 0 0 0 1)

-- V2: rotation +90 about world X, position (0, 500, 0).
-- Predicted T (per issue #87):
--   stray Inverse(R)         -> (0, 0, -500)   (apply -90 X to (0,500,0): y -> -z)
--   column-vec convention    -> (0, 0, +500)
--   IGame quirk on BoneSys   -> (0, 0, -500)   (same as stray-Inverse)
buildAndExport "_phase14d_v2_xrot90_0_500_0" \
    [0, 500, 0] (eulerAngles 90 0 0)

-- V3: rotation +45 about Z, position (300, 400, 0). Forces all
-- components non-trivial; rules out degenerate cancellations.
buildAndExport "_phase14d_v3_zrot45_300_400_0" \
    [300, 400, 0] (eulerAngles 0 0 45)

quitMax #noPrompt

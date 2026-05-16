-- Dumps Box01's transform expressed in B_Gun's LOCAL frame across
-- the animation range. The walker's Phase 14b approach assumes this
-- local TM is constant -- it captures it once at SetStaticFrame(0)
-- and lets the engine reconstruct Box01_world(t) = local_bind x
-- B_Gun_world(t). If the local TM varies across frames, the engine
-- can only reproduce it at the bind frame; everywhere else, gun
-- drifts.
--
-- Run:
--   3dsmaxbatch.exe tests/maxscript/_phase14d_b0101_local_invariance.ms ^
--     -listenerlog build/maxbatch/_phase14d_b0101_local_invariance.log -v 5

snowMax = "C:/Users/antho/Downloads/ThrREv/Ascendancy/Snowtrooper/EI_SNOWTROOPER.max"
loadMaxFile snowMax useFileUnits:true quiet:true

b01 = getNodeByName "Box01"
gun = getNodeByName "B_Gun"
if b01 == undefined or gun == undefined then (
    format "ERROR: missing Box01 or B_Gun\n"
    quitMax #noPrompt
)

format "\n========== Box01 local TM relative to B_Gun ==========\n"
format "  -- if all rows are equal across frames, walker math holds --\n"
format "  -- if any row varies, Box01 has independent animation --\n\n"

-- Sample at frame 0 + scattered frames inside the animation range +
-- a couple at the end. Adjust if your animation range differs from
-- the original (373..2488).
frames = #(0, 373, 400, 500, 800, 1200, 1800, 2488)

for t in frames do at time t (
    -- local = world * Inverse(parent_world). Row-vector convention,
    -- as Max uses internally.
    local_tm = b01.transform * (inverse gun.transform)
    format "  t=%4d:\n" t
    format "    rot row1 X = %\n" local_tm.row1
    format "    rot row2 Y = %\n" local_tm.row2
    format "    rot row3 Z = %\n" local_tm.row3
    format "    trans row4 = %\n" local_tm.row4
)

format "==========================================================\n\n"
quitMax #noPrompt

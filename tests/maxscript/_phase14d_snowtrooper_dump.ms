-- _phase14d_snowtrooper_dump.ms
--
-- Loads EI_SNOWTROOPER.max, dumps key-bone Max-side TMs at frame 0
-- (NOT current animation time -- the Phase 14c findings doc may
-- have measured at a non-zero time, which would explain the
-- chain-vs-Max discrepancy entirely), then exports for chain-dump
-- comparison via tests/maxscript/verify/_chain_dump.py.
--
-- Targets: Box01 (the gun mesh), B_Gun, B_Hand_R, B_FArm_R, Root.

fileIn "_harness.ms"
alamoTestSetup()

snowMax = "C:/Users/antho/Downloads/ThrREv/Ascendancy/Snowtrooper/EI_SNOWTROOPER.max"
loadMaxFile snowMax useFileUnits:true quiet:true

fn dumpNode lbl n =
(
    if n == undefined then (format "  % NOT FOUND\n" lbl) else (
        format "  % name=% pos=%\n" lbl n.name n.position
        format "    quat        = %\n" n.rotation
        format "    ooffsetrot  = %\n" n.objectoffsetrot
        format "    TM.row1 X   = %\n" n.transform.row1
        format "    TM.row2 Y   = %\n" n.transform.row2
        format "    TM.row3 Z   = %\n" n.transform.row3
        format "    TM.row4 T   = %\n" n.transform.row4
    )
)

at time 0 (
    format "\n========== Snowtrooper @ time 0 ==========\n"
    dumpNode "Box01     " (getNodeByName "Box01")
    dumpNode "B_Gun     " (getNodeByName "B_Gun")
    dumpNode "B_Hand_R  " (getNodeByName "B_Hand_R")
    dumpNode "B_FArm_R  " (getNodeByName "B_FArm_R")
    dumpNode "B_UArm_R  " (getNodeByName "B_UArm_R")
    dumpNode "Root      " (getNodeByName "Root")
    format "===========================================\n\n"
)

-- Export at frame 0 too (the walker samples frame 0 for the bind
-- pose). If the export at HandBone == Max-frame-0 == chain-dump,
-- then the walker is correct; the Snowtrooper visual misalignment
-- is in some other path (engine convention, AloViewer bug, etc.).
at time 0 (
    setUserProp rootNode "Alamo_Anim_Start" 0
    setUserProp rootNode "Alamo_Anim_End"   1
    setUserProp rootNode "Alamo_Anim_Name"  "ph14d_dump"
    alamoTestExport "_phase14d_snowtrooper"
)
quitMax #noPrompt

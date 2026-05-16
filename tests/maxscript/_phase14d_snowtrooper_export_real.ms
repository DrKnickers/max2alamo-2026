-- Re-exports EI_SNOWTROOPER with whatever animationRange the file
-- currently has, so the .ala captures the real animation. Logs the
-- range, slider time, and the per-frame Box01 + B_Gun world deltas
-- so we can compare them to the engine's playback later.

fileIn "_harness.ms"
alamoTestSetup()

snowMax = "C:/Users/antho/Downloads/ThrREv/Ascendancy/Snowtrooper/EI_SNOWTROOPER.max"
loadMaxFile snowMax useFileUnits:true quiet:true

format "\n========== current .max state ==========\n"
format "  sliderTime          = %\n" sliderTime
format "  animationRange      = %\n" animationRange
format "  ticksPerFrame       = %\n" ticksPerFrame
format "  frameRate           = %\n" frameRate

-- Set Alamo_Anim_* user-props to match the current animationRange so
-- the walker's Phase 11 anim path exports the right window.
animStart = animationRange.start.frame as integer
animEnd   = animationRange.end.frame as integer
setUserProp rootNode "Alamo_Anim_Start" animStart
setUserProp rootNode "Alamo_Anim_End"   animEnd
setUserProp rootNode "Alamo_Anim_Name"  "ph14d_real"

format "  Alamo_Anim_Start    = %\n" animStart
format "  Alamo_Anim_End      = %\n" animEnd
format "==========================================\n"

-- Sample some frames so we have ground truth to compare AloViewer's
-- playback against later.
b01 = getNodeByName "Box01"
gun = getNodeByName "B_Gun"
hand = getNodeByName "B_Hand_R"
sampleFrames = #(animStart, animStart + 10, animStart + 50, animStart + 200, animEnd - 100, animEnd)
format "\n========== Box01 / B_Gun / B_Hand_R world position at sample frames ==========\n"
for t in sampleFrames do at time t (
    format "  t=%6d:\n" t
    format "    Box01.row4    = %\n" b01.transform.row4
    format "    B_Gun.row4    = %\n" gun.transform.row4
    format "    B_Hand_R.row4 = %\n" hand.transform.row4
    format "    Box01-B_Gun   = %\n" (b01.transform.row4 - gun.transform.row4)
    format "    Box01-B_Hand  = %\n" (b01.transform.row4 - hand.transform.row4)
)
format "==============================================================================\n"

alamoTestExport "_phase14d_snowtrooper_real"
quitMax #noPrompt

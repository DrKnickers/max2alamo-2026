-- test_phase8_anim_single_frame (Phase 8f):
-- 1-frame animation clip (Alamo_Anim_Start=0, Alamo_Anim_End=0).
-- Catches off-by-one bugs in the per-frame walker loops.

fileIn "_harness.ms"
alamoTestSetup()

-- Max requires animationRange end > start; we use interval 0 1 here
-- but the export's Alamo_Anim_Start/End=0/0 produces a 1-frame clip
-- on disk regardless of the scene's animationRange.
animationRange = interval 0 1
bA = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "SingleFrameBone"

anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

animate on (
    at time 0 (bA.rotation = (eulerAngles 0 0 45))
    at time 0 (bA.position = [0, 0, 0])
)

setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   0
setUserProp rootNode "Alamo_Anim_Name"  "single_frame"

alamoTestExport "test_phase8_anim_single_frame"
quitMax #noPrompt

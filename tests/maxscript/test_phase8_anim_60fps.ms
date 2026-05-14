-- test_phase8_anim_60fps (Phase 8f): 60-frame clip at 60 fps.
-- Tests fps round-trip in the .ala info mini-chunk's float32 slot.

fileIn "_harness.ms"
alamoTestSetup()

frameRate = 60
animationRange = interval 0 60
bA = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "Fps60Bone"
anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"
animate on (
    at time 0  (bA.rotation = (eulerAngles 0 0 0))
    at time 60 (bA.rotation = (eulerAngles 0 0 180))
    at time 0  (bA.position = [0, 0, 0])
    at time 60 (bA.position = [0, 0, 0])
)
setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   60
setUserProp rootNode "Alamo_Anim_Name"  "fps60"
alamoTestExport "test_phase8_anim_60fps"
quitMax #noPrompt

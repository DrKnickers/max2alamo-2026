-- test_phase8_anim_long (Phase 8f):
-- 300-frame animation clip (10 seconds at 30 fps). Catches pool-size
-- scaling at high frame counts (no integer overflow in pool allocation).

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 300
bA = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "LongAnimBone"
anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

animate on (
    at time 0   (bA.rotation = (eulerAngles 0 0 0))
    at time 300 (bA.rotation = (eulerAngles 0 0 360))
    at time 0   (bA.position = [0, 0, 0])
    at time 300 (bA.position = [0, 0, 0])
)

setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   300
setUserProp rootNode "Alamo_Anim_Name"  "long_anim"

alamoTestExport "test_phase8_anim_long"
quitMax #noPrompt

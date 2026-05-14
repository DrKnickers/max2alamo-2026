-- test_phase11_multi_clip (Phase 11b):
-- Author 3 named clips (WALK 0-30, ATTACK 31-60, IDLE 61-90) on rootNode
-- via Alamo_Anim_Clips + per-clip Alamo_Anim_<NAME>_Start/_End user props.
-- Export must emit 3 sibling .ala files named <basename>_<CLIP>.ala with
-- the per-clip frame range, and must NOT emit a bare <basename>.ala.

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 90
bA = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "MultiClipBone"
anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

-- Distinct rotation per clip so the per-clip pool contents differ.
animate on (
    at time 0  (bA.rotation = (eulerAngles 0 0 0))
    at time 30 (bA.rotation = (eulerAngles 0 0 90))
    at time 60 (bA.rotation = (eulerAngles 0 0 180))
    at time 90 (bA.rotation = (eulerAngles 0 0 270))
    at time 0  (bA.position = [0, 0, 0])
    at time 90 (bA.position = [0, 0, 0])
)

setUserProp rootNode "Alamo_Anim_Clips"        "WALK|ATTACK|IDLE"
setUserProp rootNode "Alamo_Anim_WALK_Start"   0
setUserProp rootNode "Alamo_Anim_WALK_End"     30
setUserProp rootNode "Alamo_Anim_ATTACK_Start" 31
setUserProp rootNode "Alamo_Anim_ATTACK_End"   60
setUserProp rootNode "Alamo_Anim_IDLE_Start"   61
setUserProp rootNode "Alamo_Anim_IDLE_End"     90

alamoTestExport "test_phase11_multi_clip"
quitMax #noPrompt

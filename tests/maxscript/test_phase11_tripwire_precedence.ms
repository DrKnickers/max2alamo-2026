-- test_phase11_tripwire_precedence (Phase 11b tripwire B):
-- Both conventions authored on the same scene:
--   Multi-clip: Alamo_Anim_Clips = "FOO"; FOO_Start=0, FOO_End=10
--   Single:    Alamo_Anim_Start=0, Alamo_Anim_End=20, Alamo_Anim_Name="bar"
-- Multi-clip wins deterministically (suffixed path takes precedence).
-- Only <basename>_FOO.ala emits; no bare <basename>.ala.

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 20
bA = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "PrecedenceBone"
anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

animate on (
    at time 0  (bA.rotation = (eulerAngles 0 0 0))
    at time 20 (bA.rotation = (eulerAngles 0 0 90))
)

-- Multi-clip path:
setUserProp rootNode "Alamo_Anim_Clips" "FOO"
setUserProp rootNode "Alamo_Anim_FOO_Start" 0
setUserProp rootNode "Alamo_Anim_FOO_End"   10

-- Un-suffixed single-clip path (must be shadowed):
setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   20
setUserProp rootNode "Alamo_Anim_Name"  "bar"

alamoTestExport "test_phase11_tripwire_precedence"
quitMax #noPrompt

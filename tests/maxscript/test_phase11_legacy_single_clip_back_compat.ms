-- test_phase11_legacy_single_clip_back_compat (Phase 11b):
-- Author a clip using only the un-suffixed Alamo_Anim_Start/_End/_Name
-- convention (Phase 8b/c/d shape). With no Alamo_Anim_Clips prop, the
-- walker must dispatch to the single-clip path and emit a bare
-- <basename>.ala -- identical to pre-Phase-11 behavior. This is the
-- back-compat regression guard for the ~30 existing tests using this
-- convention.

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 30
bA = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "BackCompatBone"
anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

animate on (
    at time 0  (bA.rotation = (eulerAngles 0 0 0))
    at time 30 (bA.rotation = (eulerAngles 0 0 90))
    at time 0  (bA.position = [0, 0, 0])
    at time 30 (bA.position = [0, 0, 0])
)

setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   30
setUserProp rootNode "Alamo_Anim_Name"  "backcompat"

alamoTestExport "test_phase11_legacy_single_clip_back_compat"
quitMax #noPrompt

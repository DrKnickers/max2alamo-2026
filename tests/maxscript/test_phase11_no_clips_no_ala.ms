-- test_phase11_no_clips_no_ala (Phase 11b):
-- Scene with no Alamo_Anim_* user props at all. No .ala -- bare or
-- suffixed -- must be emitted (Phase 10b regression guard: animation
-- is opt-in by explicit user-prop authoring).

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 30
bA = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "NoClipBone"
anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

-- Animate the bone anyway, to prove the .ala suppression is gated on
-- the user-prop authoring, not on the absence of keyframes.
animate on (
    at time 0  (bA.rotation = (eulerAngles 0 0 0))
    at time 30 (bA.rotation = (eulerAngles 0 0 90))
)

-- Deliberately no setUserProp calls for Alamo_Anim_*.

alamoTestExport "test_phase11_no_clips_no_ala"
quitMax #noPrompt

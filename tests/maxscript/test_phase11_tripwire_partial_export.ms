-- test_phase11_tripwire_partial_export (Phase 11b tripwire A+C):
-- Three clips declared, two malformed:
--   GOOD     (0-30, both _Start/_End set, valid range)         -- emit
--   NOSTART  (no _Start prop -> sentinel -1)                   -- skip
--   INVERTED (_Start > _End)                                   -- skip
-- The walker must emit ONLY the GOOD clip and continue past
-- the malformed ones. Partial-export policy (11a wrap).

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 30
bA = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
bA.name = "TripwireBone"
anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

animate on (
    at time 0  (bA.rotation = (eulerAngles 0 0 0))
    at time 30 (bA.rotation = (eulerAngles 0 0 90))
)

setUserProp rootNode "Alamo_Anim_Clips" "GOOD|NOSTART|INVERTED"
-- GOOD: valid range.
setUserProp rootNode "Alamo_Anim_GOOD_Start" 0
setUserProp rootNode "Alamo_Anim_GOOD_End"   30
-- NOSTART: deliberately omit _Start; only set _End (walker sees start=-1).
setUserProp rootNode "Alamo_Anim_NOSTART_End"  30
-- INVERTED: _Start > _End.
setUserProp rootNode "Alamo_Anim_INVERTED_Start" 30
setUserProp rootNode "Alamo_Anim_INVERTED_End"   0

alamoTestExport "test_phase11_tripwire_partial_export"
quitMax #noPrompt

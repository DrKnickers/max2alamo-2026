-- test_spotlight_with_target (Phase 7b.2):
-- TargetSpot at [0, 0, 100] aimed at [0, 0, 0]. Pins the spotlight
-- emission path end-to-end:
--   - IGAME_TSPOT -> ExportLight::Type::Spotlight (type=2)
--   - hotspot / falloff round-trip as radians on disk (the .alo
--     stores radians; Max stores degrees in MAXScript but the
--     underlying param block is radians, so IGameProperty's float
--     gives us radians directly -- no conversion in the walker)
--   - SYNTH BONE pair: "SpotMain" at the light's position + a
--     sibling ".Target" bone at the target's world position. This
--     mirrors the vanilla EB_ICC_LANDINGPAD.ALO pattern (Spot01
--     paired with Spot01.Target).
--   - Move the target AFTER spotlight creation to verify the walker
--     reads the CURRENT target world position, not the original.

fileIn "_harness.ms"
alamoTestSetup()

sl = TargetSpot name:"SpotMain" pos:[0, 0, 100] target:(Targetobject pos:[0, 0, 0])
sl.rgb = color 255 255 255
sl.multiplier = 1.0
sl.hotspot = 30.0   -- degrees (MAXScript)
sl.falloff = 45.0   -- degrees

-- Reposition the target post-creation. Walker must read the
-- current target world TM, not whatever was set at construction.
sl.target.pos = [10, 20, 0]

format "  TargetSpot 'SpotMain' at (0,0,100), target moved to (10,20,0), cone 30/45 deg\n"

alamoTestExport "test_spotlight_with_target"
quitMax #noPrompt

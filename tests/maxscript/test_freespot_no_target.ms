-- test_freespot_no_target (Phase 7b.2):
-- FreeSpot — same chunk type=2 as TargetSpot, but no associated
-- target node. Walker should emit only the light's own bone, NOT
-- a sibling .Target.

fileIn "_harness.ms"
alamoTestSetup()

fs = FreeSpot name:"FreeSpotLight" pos:[10, 20, 30]
fs.rgb = color 255 200 100
fs.hotspot = 20.0
fs.falloff = 60.0

format "  FreeSpot at (10,20,30), cone 20/60 deg, no target node\n"

alamoTestExport "test_freespot_no_target"
quitMax #noPrompt

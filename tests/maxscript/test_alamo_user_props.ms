-- test_alamo_user_props (Phase 5d):
-- Four boxes, each exercising a different Alamo_* user property the
-- Utility command-panel writes. Validates the walker's read side --
-- the same properties round-trip from MAXScript -> exported .alo.
--
-- Pinned behaviour:
--   - "PlainBox"     no Alamo_* props
--                    -> exported normally; is_hidden=false,
--                       is_collision=false, billboard_mode=0 (default)
--   - "HiddenColl"   Alamo_Geometry_Hidden=true,
--                    Alamo_Collision_Enabled=true
--                    -> exported with is_hidden=1 and is_collision=1
--                       in its 0x402 mesh-info chunk
--   - "BillboardFace" Alamo_Billboard_Mode=2 (Face)
--                    -> its synthetic per-mesh attachment bone has
--                       billboard_mode=2 in its 0x205/0x206 chunk
--   - "SkippedMesh"  Alamo_Export_Geometry=false
--                    -> entirely absent from the .alo (no mesh chunk
--                       and no synthetic per-mesh bone)

fileIn "_harness.ms"
alamoTestSetup()

-- Plain box: no Alamo_* props, control case.
plainBox = Box length:10 width:10 height:10 pos:[0, 0, 0] name:"PlainBox"

-- Hidden + collision: set both flags via Max's user-property dict
-- (the same dict the Utility panel's SetUserPropBool writes to).
hiddenColl = Box length:10 width:10 height:10 pos:[30, 0, 0] name:"HiddenColl"
setUserProp hiddenColl "Alamo_Geometry_Hidden"   true
setUserProp hiddenColl "Alamo_Collision_Enabled" true

-- Billboard Face: mode value 2 matches the Utility panel's
-- IDC_BILLBOARD_FACE -> int 2 mapping.
billboardFace = Box length:10 width:10 height:10 pos:[60, 0, 0] name:"BillboardFace"
setUserProp billboardFace "Alamo_Billboard_Mode" 2

-- Skipped: Export_Geometry=false means the walker omits this mesh
-- entirely. Its name must NOT appear in the exported bone list.
skipped = Box length:10 width:10 height:10 pos:[90, 0, 0] name:"SkippedMesh"
setUserProp skipped "Alamo_Export_Geometry" false

format "  4 boxes authored: PlainBox (defaults), HiddenColl (hidden+collision), "
format "BillboardFace (mode=2), SkippedMesh (excluded)\n"

alamoTestExport "test_alamo_user_props"
quitMax #noPrompt

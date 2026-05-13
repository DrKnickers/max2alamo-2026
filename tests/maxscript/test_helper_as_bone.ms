-- test_helper_as_bone (Phase 5e):
-- Helpers (Dummy / Point) marked Alamo_Export_Transform=true are
-- treated as exportable bones, joining the real IGAME_BONE family in
-- the skeleton.
--
-- Three nodes:
--   - "ExportedDummy"  Dummy at (15, 25, 35), Alamo_Export_Transform=true
--                      -> appears in skeleton as a bone with that
--                         translation in its local-TM column
--   - "IgnoredDummy"   Dummy at (50, 0, 0), no Alamo_* prop
--                      -> absent from the skeleton; helpers without
--                         the opt-in prop stay scene-only
--   - "AnchorBox"      static Box at (0, 0, 0)
--                      -> still exports normally (the helper-as-bone
--                         pass doesn't disturb the mesh pass)
--
-- Pinned behaviour:
--   - 3 bones total: synthetic Root + ExportedDummy + AnchorBox's
--     per-mesh attachment bone (no IgnoredDummy)
--   - ExportedDummy's bone matrix's translation = (15, 25, 35)
--   - ExportedDummy's parent_index = 0 (Root)

fileIn "_harness.ms"
alamoTestSetup()

-- Dummy that opts in via the user prop.
ed = Dummy boxsize:[5,5,5] pos:[15, 25, 35] name:"ExportedDummy"
setUserProp ed "Alamo_Export_Transform" true

-- Dummy that does NOT opt in -- should be absent from the export.
idd = Dummy boxsize:[5,5,5] pos:[50, 0, 0] name:"IgnoredDummy"

-- Plain mesh: gets its usual per-mesh attachment bone via the
-- static-mesh path, independent of the helper pass.
anchorBox = Box length:5 width:5 height:5 pos:[0, 0, 0] name:"AnchorBox"

format "  3 nodes authored: ExportedDummy (opt-in helper), "
format "IgnoredDummy (no prop), AnchorBox (mesh)\n"

alamoTestExport "test_helper_as_bone"
quitMax #noPrompt

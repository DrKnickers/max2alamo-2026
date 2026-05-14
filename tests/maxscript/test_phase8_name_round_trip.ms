-- test_phase8_name_round_trip (Phase 8f):
-- UTF-8 byte-for-byte round-trip of bone / mesh names. Max's TCHAR is
-- UTF-16-LE on Windows; the walker's to_utf8() converts at the I/O
-- boundary. The corpus has been ASCII-only as far as we know -- this
-- test exercises Chinese, Japanese, Cyrillic, and a long Latin name to
-- catch any latent CP_ACP / CP_UTF8 / WideChar conversion bug.
--
-- Bone names authored:
--   "Hone1_骨头"     (Chinese 骨头 = "bone")
--   "Hone2_テスト"    (Japanese katakana = "test")
--   "Hone3_тест"     (Cyrillic = "test")
--   "Long_aaaaa...." (100-char Latin)
--
-- Plus an Alamo_Anim_Name with non-ASCII content (round-trip check on
-- the .ala info mini-chunk's string slot).

fileIn "_harness.ms"
alamoTestSetup()

-- BoneSys bone names go through MaxScript's wide-string -> Max wide
-- string path; should preserve all unicode code points.
b1 = BoneSys.createBone [0,  0, 0] [0, 10, 0] [0, 0, 1]
b1.name = "Hone1_骨头"
b2 = BoneSys.createBone [10, 0, 0] [10, 10, 0] [0, 0, 1]
b2.name = "Hone2_テスト"
b3 = BoneSys.createBone [20, 0, 0] [20, 10, 0] [0, 0, 1]
b3.name = "Hone3_тест"

-- 100-character Latin name (well under the 256 limit but above the
-- "everything we've ever shipped" range).
longName = "Long_"
for i = 1 to 95 do longName += "a"
b4 = BoneSys.createBone [30, 0, 0] [30, 10, 0] [0, 0, 1]
b4.name = longName

-- Anchor mesh so export has geometry.
anchorBox = Box length:5 width:5 height:5 pos:[40, 0, 0] name:"AnchorBox"

-- Note: Alamo_Anim_Name with non-ASCII set; expect round-trip through
-- the .ala info chunk's string slot (when authored animation is present
-- per Phase 8a). No animation here, so no .ala -- the name lives only
-- in the user-prop on rootNode.
setUserProp rootNode "Alamo_Anim_Name" "テスト_clip"

format "  authored bone names with non-ASCII content + 100-char long name\n"
alamoTestExport "test_phase8_name_round_trip"
quitMax #noPrompt

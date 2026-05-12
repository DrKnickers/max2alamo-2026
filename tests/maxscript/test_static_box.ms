-- test_static_box: smoke test for the basic static-prop export path.
-- Single box at origin, default Standard material. Pinned: bone count
-- (Root + per-mesh attachment bone) and identity-translation bone
-- matrix.

fileIn "_harness.ms"
alamoTestSetup()

b = Box length:30 width:40 height:20 pos:[0, 0, 0] name:"StaticBox"
format "  created Box at (0,0,0)\n"

alamoTestExport "test_static_box"
quitMax #noPrompt

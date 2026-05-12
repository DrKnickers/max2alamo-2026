-- test_two_meshes_offset: regression for PR #17 (bone WorldTM bake).
-- Two meshes at distinct world positions. Each mesh's per-mesh
-- attachment bone must carry that mesh's WorldTM translation, so the
-- meshes don't collapse to a single point in AloViewer.

fileIn "_harness.ms"
alamoTestSetup()

b = Box length:30 width:40 height:20 pos:[10, 0, 5] name:"BoxA"
s = GeoSphere radius:12 pos:[-15, 20, 5] name:"SphereB"
format "  BoxA at (10, 0, 5), SphereB at (-15, 20, 5)\n"

alamoTestExport "test_two_meshes_offset"
quitMax #noPrompt

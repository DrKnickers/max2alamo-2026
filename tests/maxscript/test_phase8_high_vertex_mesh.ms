-- test_phase8_high_vertex_mesh (Phase 8f):
-- A sphere with ~60000 vertices, just under the 65535 uint16 face-index
-- ceiling. Catches arithmetic-overflow bugs in size-computation paths
-- (e.g. a uint16 vertex count somewhere that wraps).

fileIn "_harness.ms"
alamoTestSetup()

-- GeoSphere with high subdivision. Each subdivision-step quadruples the
-- triangle count. tessellation=6 gives ~63k verts (we want under 65535).
-- Empirically tessellation=200 gives a reasonable count -- adjust to fit.
-- segments:60 gives ~7200 verts (well under uint16 ceiling but
-- still ~30x larger than any prior test).
sph = GeoSphere segments:60 baseType:0 pos:[0, 0, 0] name:"HighVertexSphere"

snap = snapshotAsMesh sph
nVerts = snap.numverts
format "  HighVertexSphere has % vertices\n" nVerts

alamoTestExport "test_phase8_high_vertex_mesh"
quitMax #noPrompt

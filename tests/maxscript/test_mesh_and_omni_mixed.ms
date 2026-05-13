-- test_mesh_and_omni_mixed (Phase 7b.1):
-- 1 mesh + 1 omni light, with the LIGHT authored first in the
-- MAXScript order. The walker must still emit (meshes ++ lights)
-- in that canonical order regardless of authoring sequence, so:
--   - connections[0] is the mesh at object_index 0
--   - connections[1] is the light at object_index 1
--
-- Catches: walker emitting in authoring order (which would put the
-- light at object_index 0 -- breaking Mike's reader, which assumes
-- meshes come first in the (meshes ++ lights) list).

fileIn "_harness.ms"
alamoTestSetup()

-- Light first in MAXScript creation order.
ol = Omnilight name:"OmniFirst" pos:[5, 5, 5]
ol.multiplier = 1.2

-- Then the mesh.
b = Box length:10 width:10 height:10 pos:[0, 0, 0] name:"BoxSecond"

format "  authored light, then mesh -- walker should still emit mesh first in connections\n"

alamoTestExport "test_mesh_and_omni_mixed"
quitMax #noPrompt

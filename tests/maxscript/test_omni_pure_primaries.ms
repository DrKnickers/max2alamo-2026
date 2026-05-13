-- test_omni_pure_primaries (Phase 7b.1):
-- Three Omni lights with pure-primary colors (R, G, B). Exercises:
--   - Multiple-light export (3 lights authored, 3 emit in same order)
--   - Channel-purity (each light has exactly one nonzero color slot)
--   - Connection-table monotonic indices when there are no meshes
--     (lights at object_index = 0, 1, 2)
--   - Per-light synthetic bones in same authoring order

fileIn "_harness.ms"
alamoTestSetup()

r = Omnilight name:"L_Red"   pos:[10, 0, 0]; r.rgb = color 255   0   0
g = Omnilight name:"L_Green" pos:[20, 0, 0]; g.rgb = color   0 255   0
b = Omnilight name:"L_Blue"  pos:[30, 0, 0]; b.rgb = color   0   0 255

format "  three primary-color omni lights authored at x=10/20/30\n"

alamoTestExport "test_omni_pure_primaries"
quitMax #noPrompt

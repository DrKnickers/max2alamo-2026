-- test_directional_light (Phase 7b.1):
-- DirectionalLight at a known position. Pins the IGAME_DIR ->
-- ExportLight::Type::Directional mapping in the walker.
--
-- Directional lights in Max have an implicit direction along the
-- node's local -Z axis (Max convention). The .alo doesn't store
-- direction explicitly; the engine reconstructs it from the bone's
-- rotation. We don't try to test render-time direction here, just
-- the type-tag emission and the synthetic-bone placement.

fileIn "_harness.ms"
alamoTestSetup()

dl = DirectionalLight name:"DirLight" pos:[0, 0, 100]
dl.rgb         = color (0.8 * 255) (0.7 * 255) (0.6 * 255)
dl.multiplier  = 0.8

format "  DirectionalLight at (0, 0, 100) authored\n"

alamoTestExport "test_directional_light"
quitMax #noPrompt

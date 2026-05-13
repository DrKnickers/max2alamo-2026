-- test_omni_light_basic (Phase 7b.1):
-- One OmniLight at a distinctive position + color + intensity.
-- Forms the baseline single-light export; pins the IGAME_OMNI ->
-- ExportLight::Type::Omni path end-to-end.
--
-- Pinned behaviour:
--   - scene.lights has exactly 1 entry, name "Omni_Basic"
--   - light type = 0 (Omni)
--   - color = (0.1, 0.2, 0.3); verifies channel ordering AND that
--     we forward Max's 0..1 colour space without gamma conversion
--   - intensity = 1.5
--   - atten_end = 100, atten_start = 30 (far-attenuation enabled)
--   - scene.bones has Root + "Omni_Basic" synthetic bone, the bone
--     positioned at (100, 200, 300) -- the light's world TM
--   - connection table: 1 entry (no meshes), light at object_index=0
--     pointing at bone#1
--
-- Note: this test is the canary for the 7b.1 walker path. If it
-- regresses, all downstream light tests will too.

fileIn "_harness.ms"
alamoTestSetup()

ol = Omnilight name:"Omni_Basic" pos:[100, 200, 300]
ol.rgb         = (color (0.1 * 255) (0.2 * 255) (0.3 * 255))
ol.multiplier  = 1.5
ol.useFarAtten = true
ol.farAttenStart = 30
ol.farAttenEnd   = 100

format "  Omni_Basic authored at (100, 200, 300), color (0.1,0.2,0.3), intensity 1.5\n"

alamoTestExport "test_omni_light_basic"
quitMax #noPrompt

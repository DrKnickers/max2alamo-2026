-- test_phase12_shadow_closed_userprop (Phase 12):
-- A closed cube with NO shadow shader but the legacy `_ALAMO_SHADOW_VOLUME`
-- user-prop = 1. The validator must still run via the user-prop trigger.
-- Cube is closed, so no warning.
--
-- Pinned behaviour:
--   - .export.log contains NO "WARNING: shadow-volume" line
--   - exercises the user-prop detection path (vs the shader-name path
--     covered by test_phase12_shadow_closed_shader)

fileIn "_harness.ms"
alamoTestSetup()

b = Box name:"ClosedShadowBoxProp" width:10 length:10 height:10 pos:[0,0,0]
-- No shadow-volume shader override; just the legacy hook user-prop.
setUserProp b "_ALAMO_SHADOW_VOLUME" 1

format "  ClosedShadowBoxProp: 6-face cube, _ALAMO_SHADOW_VOLUME=1\n"

alamoTestExport "test_phase12_shadow_closed_userprop"
quitMax #noPrompt

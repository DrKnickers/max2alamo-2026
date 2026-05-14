-- test_phase8_object_offset_scale (Phase 8f):
-- Pins the SILENT-DROP behavior of objectoffsetscale documented as a
-- Phase 5g caveat. Phase 5g composes objectoffsetrot + objectoffsetpos
-- into the bone matrix; objectoffsetscale is intentionally NOT composed
-- (non-uniform scale on bones is exotic, none of the corpus exercises
-- it, runtime semantics would require additional thought). This test
-- asserts the on-disk bone matrix's column vectors stay UNIT LENGTH
-- despite the bone having a non-identity objectoffsetscale, proving
-- the scale offset was dropped.
--
-- If a future change starts composing scale, the unit-length assertion
-- fires loudly.

fileIn "_harness.ms"
alamoTestSetup()

-- Dummy helper with non-uniform object-offset scale.
helperScaled = Dummy boxsize:[5,5,5] pos:[0, 0, 0] name:"HelperScaled"
setUserProp helperScaled "Alamo_Export_Transform" true
helperScaled.objectoffsetscale = [3, 1, 1]

-- Another bone with uniform but non-unit scale offset.
helperUniform = Dummy boxsize:[5,5,5] pos:[10, 0, 0] name:"HelperUniform"
setUserProp helperUniform "Alamo_Export_Transform" true
helperUniform.objectoffsetscale = [2, 2, 2]

anchorBox = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

format "  objectoffsetscale (HelperScaled) = %\n" helperScaled.objectoffsetscale
format "  objectoffsetscale (HelperUniform) = %\n" helperUniform.objectoffsetscale
alamoTestExport "test_phase8_object_offset_scale"
quitMax #noPrompt

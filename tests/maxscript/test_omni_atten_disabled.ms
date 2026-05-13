-- test_omni_atten_disabled (Phase 7b.1):
-- Omni with useFarAtten=false. The walker should still emit valid
-- atten_start / atten_end values without crashing; vanilla files
-- with disabled attenuation tend to leave both at 0 (or whatever
-- Max returned at the time of export).

fileIn "_harness.ms"
alamoTestSetup()

ol = Omnilight name:"Omni_NoAtten" pos:[0, 0, 50]
ol.rgb = color 200 200 200
ol.useFarAtten = false
-- Don't touch farAttenStart/End -- leave them at their Max defaults
-- so we exercise the "user never configured attenuation" path.

format "  Omni with useFarAtten=false; atten values left at Max defaults\n"

alamoTestExport "test_omni_atten_disabled"
quitMax #noPrompt

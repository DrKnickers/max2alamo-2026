-- test_omni_hidden_in_max (Phase 7b.1):
-- A hidden OmniLight should still export -- visibility translates to
-- the synthetic bone's `visible` field (0=hidden, 1=visible). The
-- engine still loads the light; whether it RENDERS the light when
-- hidden is engine policy.

fileIn "_harness.ms"
alamoTestSetup()

ol = Omnilight name:"HiddenOmni" pos:[0, 0, 0]
hide ol

format "  authored hidden OmniLight\n"

alamoTestExport "test_omni_hidden_in_max"
quitMax #noPrompt

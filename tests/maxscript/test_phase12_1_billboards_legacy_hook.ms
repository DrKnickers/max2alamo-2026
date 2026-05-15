-- test_phase12_1_billboards_legacy_hook (Phase 12.1):
-- A box with ONLY the legacy `_ALAMO_BILLBOARDS = 1` MaxScript hook
-- set (no modern `Alamo_Billboard_Mode` prop). The walker's
-- resolve_billboard_mode should fall back to the legacy hook and emit
-- billboard_mode = 1 (BBT_PARALLEL) on the synthetic bone.
--
-- Pinned behaviour:
--   - exported .alo contains a bone with billboard_mode == 1
--   - the legacy MaxScript-tooling back-compat path is exercised
--   - if a future regression removes the legacy fallback, the bone's
--     billboard_mode will read 0 (Disable) and this test fails

fileIn "_harness.ms"
alamoTestSetup()

b = Box name:"LegacyBillboardBox" width:10 length:10 height:10 pos:[0,0,0]
-- Legacy hook only -- intentionally omit Alamo_Billboard_Mode.
setUserProp b "_ALAMO_BILLBOARDS" 1
format "  LegacyBillboardBox: _ALAMO_BILLBOARDS=1 (no modern Alamo_Billboard_Mode)\n"

alamoTestExport "test_phase12_1_billboards_legacy_hook"
quitMax #noPrompt

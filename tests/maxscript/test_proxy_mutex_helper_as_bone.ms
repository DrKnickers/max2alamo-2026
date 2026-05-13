-- test_proxy_mutex_helper_as_bone (Phase 7c.2):
-- Mutex between Alamo_Proxy detection (7c.2) and helper-as-bone
-- (Phase 5e). Two helpers:
--   - "p_proxy"  Alamo_Proxy with Alamo_Export_Transform=true
--                -> exports as a PROXY (proxy detection wins);
--                   the Alamo_Export_Transform=true is ignored
--   - "MyDummy"  plain Dummy with Alamo_Export_Transform=true
--                -> exports as a BONE (Phase 5e behavior preserved)
--
-- Catches: the walker double-counting a proxy as a bone (would
-- result in duplicate "p_proxy" entries OR a proxy that doesn't
-- get the .Target-style synth-bone treatment.)

fileIn "_harness.ms"
alamoTestSetup()

p = Alamo_Proxy name:"p_proxy" pos:[0, 0, 0]
setUserProp p "Alamo_Export_Transform" true

d = Dummy boxsize:[5, 5, 5] pos:[10, 0, 0] name:"MyDummy"
setUserProp d "Alamo_Export_Transform" true

format "  authored Alamo_Proxy + plain Dummy, both marked Export_Transform\n"

alamoTestExport "test_proxy_mutex_helper_as_bone"
quitMax #noPrompt

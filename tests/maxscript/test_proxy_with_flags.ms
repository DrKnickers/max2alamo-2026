-- test_proxy_with_flags (Phase 7c.2):
-- Four proxies covering the 2x2 cross-product of optional flags
-- (Alamo_Geometry_Hidden, Alamo_Alt_Decrease_Stay_Hidden). Pins:
--   - default: only mini 5+6 emitted in the 0x603 chunk
--   - hidden=true: mini 7 emitted
--   - alt_dec=true: mini 8 emitted
--   - both=true:   mini 7 + 8 emitted
-- (The Phase 7a writer suppresses optional mini-chunks for default-
-- false values; this test exercises the read side proving the
-- walker actually sets the flags when the props are set.)

fileIn "_harness.ms"
alamoTestSetup()

a = Alamo_Proxy name:"p_defaults" pos:[0, 0, 0]

b = Alamo_Proxy name:"p_hidden" pos:[10, 0, 0]
setUserProp b "Alamo_Geometry_Hidden" true

c = Alamo_Proxy name:"p_alt_dec" pos:[20, 0, 0]
setUserProp c "Alamo_Alt_Decrease_Stay_Hidden" true

d = Alamo_Proxy name:"p_both" pos:[30, 0, 0]
setUserProp d "Alamo_Geometry_Hidden" true
setUserProp d "Alamo_Alt_Decrease_Stay_Hidden" true

format "  4 proxies: defaults / hidden / alt_dec / both\n"

alamoTestExport "test_proxy_with_flags"
quitMax #noPrompt

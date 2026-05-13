-- test_proxy_basic (Phase 7c.2):
-- One Alamo_Proxy helper at a distinctive position. Pins the
-- IGAME_HELPER + ClassID(kAlamoProxyClassID) detection path:
--   - scene.proxies has 1 entry, name matches the node name
--   - synth bone at the proxy's world TM
--   - 0x603 chunk has mini 5 (name) + mini 6 (bone); no mini 7 or 8
--     (defaults suppressed per vanilla)
--   - connections.count = 0 (no meshes/lights); nProxies = 1

fileIn "_harness.ms"
alamoTestSetup()

p = Alamo_Proxy name:"p_engine_glow" pos:[50, 100, 200]

format "  Alamo_Proxy 'p_engine_glow' authored at (50, 100, 200)\n"

alamoTestExport "test_proxy_basic"
quitMax #noPrompt

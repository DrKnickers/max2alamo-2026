-- test_proxy_mesh_light_combined (Phase 7c.2):
-- 1 mesh + 1 Omni light + 2 Alamo_Proxy helpers in one scene.
-- Pins the connection-table accounting:
--   - nConnections = 2 (mesh + light); proxies are NOT connection objects
--   - nProxies = 2
--   - connection object_indices monotonic 0, 1 (mesh first, then light)
--   - skeleton has Root + mesh-bone + light-bone + 2 proxy-bones = 5

fileIn "_harness.ms"
alamoTestSetup()

m = Box length:10 width:10 height:10 pos:[0, 0, 0] name:"Box"
ol = Omnilight name:"Omni" pos:[20, 0, 0]
p1 = Alamo_Proxy name:"p_a" pos:[30, 0, 0]
p2 = Alamo_Proxy name:"p_b" pos:[40, 0, 0]

format "  1 mesh + 1 light + 2 proxies authored\n"

alamoTestExport "test_proxy_mesh_light_combined"
quitMax #noPrompt

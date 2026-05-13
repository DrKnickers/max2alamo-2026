-- test_dummy_not_a_proxy (Phase 7c.2):
-- A plain Dummy helper named "p_smoke" should NOT export as a proxy --
-- only Alamo_Proxy class instances do. Proves the walker uses
-- Class_ID detection, not name-prefix matching.
--
-- Also asserts the Alamo_Proxy alongside it DOES export, so the two
-- code paths don't accidentally trigger each other.

fileIn "_harness.ms"
alamoTestSetup()

-- Plain Dummy named "p_smoke" -- DESPITE the legacy-looking prefix,
-- the walker shouldn't pick this up. Mike Lankamp's importer also
-- only knows the Alamo_Proxy plugin class, not Dummy.
d = Dummy boxsize:[5, 5, 5] pos:[10, 0, 0] name:"p_smoke"

-- The real proxy.
p = Alamo_Proxy name:"p_real_proxy" pos:[20, 0, 0]

format "  authored: 1 plain Dummy 'p_smoke', 1 Alamo_Proxy 'p_real_proxy'\n"

alamoTestExport "test_dummy_not_a_proxy"
quitMax #noPrompt

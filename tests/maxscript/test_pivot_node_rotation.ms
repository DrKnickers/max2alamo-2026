-- test_pivot_node_rotation (Phase 5g, sanity check):
-- Authors a BoneSys bone and a Dummy helper with NODE-LEVEL rotation
-- (b.rotation = ... / d.rotation = ...). This path has always worked --
-- IGameNode::GetLocalTM/GetWorldTM captures node rotation. The test is
-- a guardrail: if the Phase 5g composition somehow breaks the existing
-- node-rotation path, this test catches it.
--
-- Authored rotation: eulerAngles 0 0 -90 (Max-flavour -90deg about world
-- Z). Empirical observation from probe_pivot_orientation*.ms: this
-- produces bone-matrix column 1 ("local +Y in parent space") ~= (-1, 0, 0).
--
-- Pinned (per verify_test_pivot_node_rotation.py):
--   - BoneRotated bone: col1 ~= (-1, 0, 0)  (rotation captured)
--   - DummyRotated bone: col1 ~= (-1, 0, 0)  (rotation captured)
--   - Both bones have unit-length, orthogonal column basis

fileIn "_harness.ms"
alamoTestSetup()

b_rot = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
b_rot.name = "BoneRotated"
b_rot.rotation = (eulerAngles 0 0 -90)

d_rot = Dummy boxsize:[5, 5, 5] pos:[0, 40, 0] name:"DummyRotated"
d_rot.rotation = (eulerAngles 0 0 -90)
setUserProp d_rot "Alamo_Export_Transform" true

-- Anchor mesh so the export has actual geometry to write.
hull = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"Hull"

format "  BoneRotated + DummyRotated authored with node rotation eulerAngles 0 0 -90\n"

alamoTestExport "test_pivot_node_rotation"
quitMax #noPrompt

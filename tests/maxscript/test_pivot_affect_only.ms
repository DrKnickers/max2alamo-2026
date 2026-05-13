-- test_pivot_affect_only (Phase 5g, regression test for issue #53):
-- The PG hardpoint workflow is to rotate the PIVOT of a Dummy/Point
-- helper (Hierarchy panel -> Affect Pivot Only, equivalent to MaxScript
-- `node.objectoffsetrot = ...`) to encode firing direction. Before
-- Phase 5g this rotation was silently lost on export. Now the walker
-- composes object offset into the on-disk bone matrix.
--
-- Authored offset: quaternion equivalent of -90deg about world Z. With
-- the fix in place this produces bone-matrix col1 ("local +Y in parent
-- space") ~= (-1, 0, 0) -- matching what node-level rotation by the
-- same angle produces in test_pivot_node_rotation.
--
-- Scope note: only DUMMY helpers are tested here, because that is the
-- canonical hardpoint authoring pattern (Dummy/Point + Alamo_Export_
-- Transform=true). BoneSys bones interact with objectoffsetrot via
-- Max-internal logic that double-applies the rotation; using a BoneSys
-- bone for an "Affect Pivot Only" hardpoint produces a 180-deg-off
-- result regardless of whether our walker composes the offset. Users
-- should set BoneSys bone direction via `b.rotation = ...` or the
-- interactive Rotate tool in Normal mode instead. See issue #53 caveat
-- and probe_pivot_orientation_2.ms history.
--
-- Pinned (per verify_test_pivot_affect_only.py):
--   - DummyPivoted bone: col1 ~= (-1, 0, 0)  (offset captured)
--   - DummyPivoted column basis is unit-length and orthogonal
--
-- This test FAILS before the Phase 5g fix lands.

fileIn "_harness.ms"
alamoTestSetup()

-- Dummy with -90deg-about-Z Affect-Pivot-Only rotation. Opts in as a
-- helper-as-bone (Phase 5e path), so the walker emits a bone for it.
d_pivot = Dummy boxsize:[5, 5, 5] pos:[0, 40, 0] name:"DummyPivoted"
d_pivot.objectoffsetrot = (quat 0 0 -0.7071068 0.7071068)
setUserProp d_pivot "Alamo_Export_Transform" true

-- Anchor mesh so the export has actual geometry to write.
hull = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"Hull"

format "  DummyPivoted authored via objectoffsetrot (-90deg about Z)\n"

alamoTestExport "test_pivot_affect_only"
quitMax #noPrompt

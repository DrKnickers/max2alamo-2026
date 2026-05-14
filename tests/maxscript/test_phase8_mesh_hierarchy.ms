-- test_phase8_mesh_hierarchy (Phase 8f):
-- Pins the WALKER'S local-only visibility convention against a future
-- change. Mesh-as-parent + bone-parented-to-mesh + a HIDDEN parent
-- with VISIBLE children -- exactly the pattern we see in vanilla
-- `EB_AATURRET.ALO` (bone[11] `girder` HIDDEN + bone[12]
-- `p_girder_sparks00` also HIDDEN -- artist marked the child redundantly
-- proving the convention is per-node-explicit, not inherited).
--
-- The walker today reads `INode::IsNodeHidden()` per node with no
-- ancestor walking. This test ASSERTS that behavior: hiding BoxParent
-- in Max does NOT make BoxMid's bone.visible turn false on disk.
-- Corpus survey confirmed 0/2066 vanilla files have hidden parent +
-- visible descendant; Mike Lankamp's and Gaukler's importers both
-- treat visibility per-node; this test pins it.
--
-- Scene composition:
--   BoxParent (Box, ISHIDDEN=true in Max)
--   BoxMid    (Box, child of BoxParent, NOT hidden)
--   BoxLeaf   (Box, child of BoxMid, NOT hidden)
--   LinkedBone (BoneSys bone child of BoxMid - bone parented to mesh)
--
-- Expected on disk:
--   BoxParent  bone.visible = false  (Max-hidden flag honored)
--   BoxMid     bone.visible = true   (local-only convention; parent's
--                                     hidden state does NOT propagate)
--   BoxLeaf    bone.visible = true   (same)
--   LinkedBone bone.visible = true
--
-- Plus a no-animation export (no .ala emitted; test only pins static
-- bone.visible flags from the .alo's 0x205 chunks).

fileIn "_harness.ms"
alamoTestSetup()

boxParent = Box length:10 width:10 height:10 pos:[0, 0, 0] name:"BoxParent"
boxMid    = Box length:8 width:8 height:8 pos:[20, 0, 0] name:"BoxMid"
boxMid.parent = boxParent

boxLeaf   = Box length:6 width:6 height:6 pos:[40, 0, 0] name:"BoxLeaf"
boxLeaf.parent = boxMid

linkedBone = BoneSys.createBone [20, 10, 0] [20, 30, 0] [0, 0, 1]
linkedBone.name = "LinkedBone"
linkedBone.parent = boxMid

-- Hide ONLY BoxParent. Per Max-viewport semantics this visually hides
-- the whole subtree; per Max SDK semantics each node's IsNodeHidden is
-- queried locally and returns FALSE for the children. We assert the
-- on-disk export captures the SDK reading, not the viewport reading.
boxParent.isHidden = true

format "  isHidden flags: parent=%, mid=%, leaf=%, bone=%\n" \
    boxParent.isHidden boxMid.isHidden boxLeaf.isHidden linkedBone.isHidden

alamoTestExport "test_phase8_mesh_hierarchy"
quitMax #noPrompt

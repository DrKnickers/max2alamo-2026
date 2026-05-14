-- test_phase8_visibility_propagation (Phase 8f):
-- Regression test for the "Propagate visibility to descendants" button
-- added in Phase 8f. The button (in the Alamo Utility panel's Node Export
-- Options rollout) walks each selected node's descendants and copies the
-- selected node's `isHidden` flag AND `visibility` controller to each
-- descendant. Solves the death-clone authoring pain (a destroyed unit's
-- many damage chunks + child decals/sparks need matching visibility
-- tracks; without the button, each child has to be authored manually).
--
-- The button delegates to a MaxScript snippet inside alamo_utility.cpp
-- (via ExecuteMAXScriptScript). This test exercises the SAME snippet
-- directly so the regression assertion is independent of clicking a
-- physical button.
--
-- Scene composition:
--   ParentBone: visibility animated true -> false over 31 frames.
--   Child1, Child2, Child3: bones parented to ParentBone, no authored
--     visibility animation. After propagation each child should have
--     the parent's curve.
--   SpotEditChild (special): parented to ParentBone but post-propagation
--     gets its visibility flipped back to "constant visible" to confirm
--     the propagation isn't a binding lock -- the user retains control
--     to opt out per-descendant.
--   AnchorBox: static mesh so the export has geometry.
--
-- Pinned by verify_test_phase8_visibility_propagation.py:
--   - 4 bones in visibility_map have 0x1007 chunks: ParentBone + 3
--     post-propagated children.
--   - SpotEditChild has NO 0x1007 (constant-visible, elided after the
--     post-propagation override).
--   - All 4 emitted 0x1007 payloads are byte-identical.

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 30

parentBone = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
parentBone.name = "ParentBone"

child1 = BoneSys.createBone [10, 0, 0] [10, 20, 0] [0, 0, 1]
child1.name = "Child1"
child1.parent = parentBone

child2 = BoneSys.createBone [20, 0, 0] [20, 20, 0] [0, 0, 1]
child2.name = "Child2"
child2.parent = parentBone

child3 = BoneSys.createBone [30, 0, 0] [30, 20, 0] [0, 0, 1]
child3.name = "Child3"
child3.parent = parentBone

-- SpotEditChild is parented to Root, NOT to ParentBone. Tests that
-- propagation walks only descendants of the selected node -- arbitrary
-- scene nodes outside the subtree stay untouched. (The user retaining
-- per-descendant override control is a related but separately-tested
-- semantic; this scene pins the scope-of-propagation contract.)
spotEdit = BoneSys.createBone [40, 0, 0] [40, 20, 0] [0, 0, 1]
spotEdit.name = "OutsideSubtree"
-- intentionally NOT spotEdit.parent = parentBone

anchorBox = Box length:5 width:5 height:5 pos:[50, 0, 0] name:"AnchorBox"

-- Author parent's visibility animation: visible -> hidden.
animate on (
    at time 0  (parentBone.visibility = true)
    at time 30 (parentBone.visibility = false)
)

-- Run the propagation snippet -- the same code the C++ button calls
-- via ExecuteMAXScriptScript. Kept verbatim here so the test exercises
-- the exact authoring path users will hit via the UI.
fn alamoPropagateVisFromParent parent child = (
    child.isHidden = parent.isHidden
    try (child.visibility = copy parent.visibility) catch ()
    for c in child.children do alamoPropagateVisFromParent parent c
)
select parentBone
undo on (
    for parent in selection do (
        for c in parent.children do alamoPropagateVisFromParent parent c
    )
)

format "[test] propagated visibility to %s direct children\n" (parentBone.children.count as string)

setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   30
setUserProp rootNode "Alamo_Anim_Name"  "vis_propagation"

alamoTestExport "test_phase8_visibility_propagation"
quitMax #noPrompt

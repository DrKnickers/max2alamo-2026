-- test_visibility_blinking_light (Phase 8d):
-- An Omni light that's visible at frame 0 and hidden by frame 30. The
-- walker's third animation pass samples INode::GetVisibility(t) per
-- frame, thresholds at >= 0.5, and emits a 0x1007 chunk inside the
-- light's per-bone 0x1002 container carrying the bit-packed visibility
-- mask.
--
-- Notes on Max 2026's on_off visibility controller (load-bearing for
-- this test's design):
--   - Max installs an on_off Boolean controller on every node's
--     visibility track. None of the documented MaxScript patterns for
--     replacing it with a clean bezier_float succeed
--     (.visibility=bezier_float, sub-anim controller swap,
--     remove/addVisibilityTrack, setVisController). We work with
--     on_off.
--   - INode::GetVisibility(t) (the C++ accessor the walker uses) reads
--     the controller's value as a FLOAT in [0,1]. MaxScript's
--     ol.visibility read returns a BOOLEAN (the float thresholded).
--     These two views can disagree by a few frames around a transition,
--     so the test does NOT pin the exact transition frame -- it pins
--     only what's invariant:
--       * the chunk gets emitted (because frame 30 is hidden)
--       * BlinkingLight starts visible (frame 0 is at a TRUE key)
--       * BlinkingLight ends hidden (frame 30 is at a FALSE key)
--       * some transition happens (payload is neither all-FF nor all-00)
--       * the unused trailing bit (beyond frame 30) is clear
--       * AlwaysVisible (no visibility animation) has NO 0x1007
--       * no other bone has 0x1007
--
-- Together these pin pack_visibility_bits + the >= 0.5 threshold + the
-- per-bone emission gate + the visibility_map plumbing without
-- depending on Max's float interpolation reaching a specific bit at a
-- specific frame.

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 30

ol = Omnilight name:"BlinkingLight" pos:[0, 0, 50]

-- Two keys at the range endpoints. Max's on_off controller produces a
-- float that goes from 1.0 at frame 0 to 0.0 at frame 30. The walker
-- thresholds at >= 0.5 -> visible frames in the first half, hidden in
-- the second. The exact crossing frame depends on Max's tangents.
animate on
(
    at time 0  (ol.visibility = true)
    at time 30 (ol.visibility = false)
)

-- A control bone with no visibility animation. The verifier asserts
-- NO 0x1007 chunk gets emitted for it (constant-visible elision).
b = BoneSys.createBone [0, 0, 0] [0, 20, 0] [0, 0, 1]
b.name = "AlwaysVisible"

-- Anchor mesh so the export has geometry to walk past. No visibility
-- animation -> no 0x1007 either.
hull = Box length:5 width:5 height:5 pos:[20, 0, 0] name:"AnchorBox"

vis_at_0  = at time 0  ol.visibility
vis_at_30 = at time 30 ol.visibility
format "  BlinkingLight visibility @ frame 0  = % (expected true)\n" vis_at_0
format "  BlinkingLight visibility @ frame 30 = % (expected false)\n" vis_at_30

setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   30
setUserProp rootNode "Alamo_Anim_Name"  "blink_test"

format "  clip: Alamo_Anim_Start=0  Alamo_Anim_End=30  Alamo_Anim_Name=blink_test\n"

alamoTestExport "test_visibility_blinking_light"
quitMax #noPrompt

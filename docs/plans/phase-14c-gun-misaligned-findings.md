# Phase 14c — Gun-mesh misalignment findings

> Status: investigation complete; fix scope unresolved. The data shows a **deeper convention mismatch** than any single H-A/B/C/D hypothesis from the investigation plan accounts for. A Phase 14d follow-up needs to investigate AloViewer's actual matrix-composition convention (most likely candidate: column-vector composition vs. our row-vector encoding assumption) before a fix can land.

## Three views of the gun's bind-pose position

| Source | World translation `(x, y, z)` | How computed |
|---|---|---|
| **Max** (truth) | `(6.77, -1.56, 5.79)` | `Box01.transform.position` from `tests/maxscript/_phase14c_dump.ms` running against `EI_SNOWTROOPER.max` |
| **Row-vector chain composition of our .alo** | `(-4.493, -1.055, 0.000)` | `_chain_dump.py` composing the `.alo`'s parent chain from Box01 → B_Gun → ... → Root via row-vector convention |
| **Visible** (AloViewer) | gun renders at far-left of viewport, body at right (Z ≈ 0 = floor) | Visual inspection of `build/maxbatch/phase14b_snowtrooper.alo` |

The row-vector chain composition disagrees sharply with Max. The visible-in-AloViewer position **qualitatively matches** the row-vector composition's prediction (gun at floor height, far-left in the viewport per the post-Phase-14b screenshot) — but this is an eyeball cross-check against a single screenshot, not a measured engine-renders-here observation. The mismatch is between **Max's view** and **what AloViewer renders**, with the row-vector composition serving as one specific candidate model for the engine's behavior.

## Side-by-side bone-chain deltas

| Node | Max world | Row-vector chain world | Delta (Max - Engine) |
|---|---|---|---|
| B_Hand_R | `(-0.934, -2.322, 4.490)` | `(-4.106, +0.016, 7.763)` | `(+3.17, -2.34, -3.27)` |
| B_Gun | `(-0.822, -2.614, 4.720)` | `(-4.412, -0.211, 7.689)` | `(+3.59, -2.40, -2.97)` |
| Box01 | `(+6.765, -1.555, 5.792)` | `(-4.493, -1.055, 0.000)` | `(+11.26, -0.50, +5.79)` |

## Sharpest clue — the synthetic fixture's exact inverse rotation

In the Phase 14b test fixture (`test_phase14b_static_mesh_parent_bone.ms`), `HandBone` is authored at Max world `(500, 0, 0)` with bone direction `+Y` (rotation = `+90°` about world Z, stored in the node TM). The `.alo`'s encoding of HandBone's translation row is `(0, -500, 0)`.

That value is **exactly** `(500, 0, 0) × Inverse(R_+90°_about_Z)` — a clean asymmetric rotation, not a basis swap, not a small numerical drift. The pattern is a single applied inverse rotation, on a top-level bone with identity object offset.

This observation is the highest-resolution discriminator we have, because:

- It's on a **top-level** bone, eliminating any "deep-chain accumulation" explanation.
- The bone has **identity object offset**, eliminating any "missed compose_with_object_offset" explanation.
- The applied transform is **exactly** `Inverse(R)` for the bone's own rotation `R`, not a generic transform. That points sharply at one of: a stray `Inverse()` call in the walker, a column-vector convention mismatch (which mathematically equals applying `Inverse(R)` on top of row-vector results for non-identity rotations), or an `IGameNode::GetLocalTM` SDK quirk that returns world-with-inverse-rotated-translation rather than plain world.

Phase 14d's Task 1 should vary HandBone's rotation through several known asymmetric values (identity, `+45°` about X, `+90°` about Y) and dump the resulting `.alo` translation row in each case. The pattern of which inverse rotation appears (or doesn't) discriminates between the three candidate explanations cheaply.

## Observations

1. **All object offsets are identity** in the Snowtrooper rig. Both H-A ("authoring with non-trivial offset") and the "compose_with_object_offset double-application" angle are not the cause.

2. **Body bones don't match Max either.** B_Hand_R is ~5 units off from where Max says it is. Yet the body still **renders correctly** in AloViewer (a recognizable upright Snowtrooper, per the post-Phase-14b screenshot). This means AloViewer's chain composition produces internally-consistent body geometry without matching Max's world coordinates. The body's relative pose (bones meet at correct joints) is preserved; the absolute world placement is shifted by a roughly-systematic transform.

3. **The gun's delta is much larger than the body bones'.** Body bones drift by ~3-5 units. The gun drifts by ~11 units on X plus ~6 on Z (sitting on the floor instead of at chest height). If a single transform explained both, the deltas would be uniform. They aren't.

4. **The Phase 14b synthetic fixture exhibits the same bug.** HandBone, a top-level bone in the test scene, was authored at Max world `(500, 0, 0)` but its `.alo` encoding chain-composes to `(0, -500, 0)`. This rules out "the bug only happens deep in chains" — even a single-level top-level bone shows the discrepancy.

## Why the four original hypotheses don't fit cleanly

| # | Hypothesis | Why the data doesn't fit |
|---|---|---|
| H-A | **Authoring**: gun set up at a far-from-body position in the .max file. | Max's gun position `(6.77, -1.56, 5.79)` is `(7.59, 1.05, 1.07)` away from B_Gun. That's plausible "rifle held at arms length"; gun is not at a wildly wrong setup position. The discrepancy is in our walker's encoding, not the authoring. |
| H-B | **Walker math** (multiplication order or basis error in `mesh_world * Inverse(parent_world)`). | The body's bones — encoded through `walk_bones`, NOT through the Phase 14b `walk_node` branch — also show the same kind of systematic offset from Max's view. So this isn't isolated to the Phase 14b code path. |
| H-C | **Engine convention** (AloViewer composes differently than the .alo spec). | Plausible candidate. The body renders correctly relative to its own bones, suggesting AloViewer's composition is internally consistent — but the engine's positions don't match Max's world. |
| H-D | **Vertex space** (mesh vertices in the wrong frame after the synth-bone change). | Pre-Phase-14b, Box01 attached to Root with the mesh's world TM, and the gun rendered roughly at the body's hand (per the user's pre-fix screenshot). Post-Phase-14b, the gun renders at the floor far from the body. So the bug appeared with the Phase 14b mesh-parent-inheritance change. Vertices themselves probably aren't the issue — only the parent-relative encoding choice. |

## What the data actually points to

The most likely explanation, given:
- The body renders correctly in AloViewer despite chain-composed world coords not matching Max's,
- Even top-level bones in synthetic test scenes show the same mismatch,
- All object offsets are identity,

is that **AloViewer's matrix-composition convention differs from the row-vector `child * parent` chain composition assumed by `_chain_dump.py` and by the Phase 14b walker fix.** Plausible candidates:

- **Column-vector convention.** AloViewer might compose matrices as `parent * child` (with column vectors), where our row-vector `child * parent` gives different results for non-commutative rotations. In that case our walker's `mesh_world * Inverse(parent_world)` is using the wrong multiplication order.
- **Transposed matrix interpretation.** The 12-float on-disk layout (per `encode_matrix3` and Mike Lankamp's reader's [c[1],c[5],c[9]]... reconstruction) could be storing columns-as-rows in a way that compounds across chain depth.

The body renders correctly because all bones are encoded consistently — when the engine composes them with its (possibly mismatched-to-Max) convention, the result is internally coherent (body parts attach at correct joints). The gun is at the wrong place because the Phase 14b `walk_node` change computed `mesh_world * Inverse(parent_world)` in Max's coord-space, then encoded it for an engine that composes it differently than Max would.

## Recommended next step (Phase 14d)

Before any walker change:

1. **Confirm AloViewer's composition convention.** Either:
   - Read AloViewer's source (`src/Assets/Models.cpp` or wherever bone composition lives) on GitHub.
   - Empirically: author a single-bone test where the bone's world position has a known asymmetric rotation+translation, export, load in AloViewer, eyeball whether the bone ends up at Max's world position or somewhere transformed.

2. **Once the convention is known**, the walker's `mesh_world * Inverse(parent_world)` becomes either the right or wrong direction. Fix that, and the `verify_test_phase14b_static_mesh_parent_bone.py` translation assertion (`(200, 0, 0)`) needs to be recomputed to match.

3. **The body's apparent-but-untrue chain composition** (body looks right, but bone positions don't match Max) might be evidence of a long-standing systematic offset that future Phase 8 work would need to address — likely related to whatever convention difference is exposed in Phase 14d. Could be folded into the same fix.

## Open questions

- **Why does the body render correctly visually if engine-composed world coords don't match Max?** Engine composition might use a different convention than what the .alo's on-disk layout literally encodes; the engine + writer are both using the "wrong" convention consistently, and the result looks fine until something (the Phase 14b change) reaches across that convention boundary.

## Files

- `tests/maxscript/verify/_chain_dump.py` (committed as `58ff68d`) — the diagnostic that produced the engine-chain numbers.
- `tests/maxscript/_phase14c_dump.ms` (untracked, user-specific Downloads path) — the diagnostic that produced Max's numbers.
- This document.

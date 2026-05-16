# Phase 14d — gun-mesh misalignment: not a walker bug

> Status: investigation closed without a walker change. Two distinct
> root causes converge to produce the symptom Phase 14b set out to
> fix; both are content-authoring issues. Companion to
> [`phase-14c-gun-misaligned-findings.md`](phase-14c-gun-misaligned-findings.md),
> which characterised the symptom but reached the wrong conclusion
> (postulated a walker convention bug). See [#87](https://github.com/DrKnickers/max2alamo-2026/issues/87).

## TL;DR

The walker correctly samples every node at frame 0
(`igame->SetStaticFrame(0)` at [`scene_walker.cpp:1966`](../../max2alamo/src/scene_walker.cpp:1966))
and encodes whatever it finds there into the `.alo`. AloViewer
renders the bind pose at exactly the encoded position. There is no
column-vector convention bug, no stray `Inverse(R)` call, no IGame
SDK quirk on BoneSys bones. Phase 14c's "(0, -500, 0) clue" was a
coincidence specific to a self-corrupting fixture, not a walker
signature.

## What was actually wrong

### Cause A — synthetic fixture's keyframe-orbit corrupted the bind pose

[`tests/maxscript/test_phase14b_static_mesh_parent_bone.ms`](../../tests/maxscript/test_phase14b_static_mesh_parent_bone.ms)
authored `HandBone` like this (pre-fix):

```maxscript
hand = BoneSys.createBone [500, 0, 0] [500, 1000, 0] [0, 0, 1]   -- direction +Y
hand.name = "HandBone"
animate on (
    at time 0  (hand.rotation = (eulerAngles 0 0 0))
    at time 10 (hand.rotation = (eulerAngles 0 0 45))
)
```

`createBone` with from-to direction +Y bakes `+90° Z` into the bone's
node TM (verified via Max-side dump:
`hand.transform.row1 = (0, 1, 0)`, `row4 = (500, 0, 0)`). The
`at time 0 (hand.rotation = (eulerAngles 0 0 0))` keyframe then
**orbits the translation around the local pivot** as Max applies the
rotation delta to undo the baked-in rotation:

```
post-orbit position = (500, 0, 0) × R(-90° Z) = (0, -500, 0)
```

At frame 0 (where the walker samples), HandBone is *actually* at
world `(0, -500, 0)` with identity rotation. The walker faithfully
encodes that. AloViewer faithfully renders it.

Verified by replacing the createBone direction with `+X` (collinear-X
from-to), which leaves node TM at identity rotation — the keyframe
becomes a no-op for translation, and HandBone encodes as
`(500, 0, 0)` as the fixture intended. See the post-fix
`test_phase14b_static_mesh_parent_bone.ms` and updated verifier
expectations (`HandBone.translation = (500, 0, 0)`,
`GunMesh.translation = (0, 200, 0)` parent-local).

### Cause B — EI_SNOWTROOPER's frame 0 is outside its animationRange

`EI_SNOWTROOPER.max` was authored with:

```
sliderTime              = 373f
animationRange          = (interval 373f 2488f)
Box01.position@current  = [6.76, -1.56, 5.79]    -- in the hand at frame 373
```

At frame 0 (where the walker samples for the bind pose),
`Box01.position` falls back to its `Position_XYZ` controller's
leftover initial state of `[-4.49, -1.06, 0]` — on the floor,
far-left of the body. **Box01 IS rigidly parented to B_Gun via the
artist's keyframes** (`|Box01 - B_Gun| = 7.7356` at every frame ≥ 1,
invariant to frame value), but the parenting only takes effect inside
the animation range.

The chain-composed encoded `.alo` matches Max's frame-0 view *exactly*
for every bone tested:

| Bone | Max frame-0 world | Chain-composed encoded world |
|---|---|---|
| B_FArm_R | `(-2.629, -0.046, 7.793)` | `(-2.629, -0.046, 7.793)` |
| B_Hand_R | `(-4.106, 0.016, 7.763)` | `(-4.106, 0.016, 7.763)` |
| B_Gun | `(-4.412, -0.211, 7.689)` | `(-4.412, -0.211, 7.689)` |
| Box01 | `(-4.493, -1.055, 5e-6)` | `(-4.493, -1.055, 0.000)` |

No discrepancy. Walker correctness confirmed against real user
content.

## Why Phase 14c reached the wrong conclusion

Phase 14c compared chain-composed encoded values against a Max-side
dump (`_phase14c_dump.ms`, uncommitted). The discrepancies it
reported were either:

- The synthetic fixture's self-corrupted bind pose (Cause A above),
  which produced the "exact `Inverse(R_+90°_Z)`" pattern that *looked*
  like a walker signature but was just one specific fixture's orbit
  arithmetic; **OR**
- Snowtrooper bone positions sampled at the file's `sliderTime` (373)
  on the Max side vs the walker's frame 0 on the encoding side. A
  same-time-on-both-sides comparison (Phase 14d's
  `_phase14d_snowtrooper_dump.ms` with explicit `at time 0 (...)`)
  shows perfect agreement.

The chain-composition math in
[`tests/maxscript/verify/_chain_dump.py`](../../tests/maxscript/verify/_chain_dump.py)
was correct. So was the walker. The disagreement was an artefact of
sampling time, not convention.

## What the four diagnostic variants confirmed

[`tests/maxscript/_phase14d_variants.ms`](../../tests/maxscript/_phase14d_variants.ms)
authored four standalone single-bone scenes and compared Max-side
`node.transform.row4` to the encoded `.alo` translation row:

| Variant | Max TM.row4 (T) | Encoded T | Match |
|---|---|---|---|
| V0 (rot `eulerAngles 0 0 90`, pos `(500, 0, 0)`) | `(500, 0, 0)` | `(500, 0, 0)` | ✓ |
| V1 (identity, `(500, 0, 0)`) | `(500, 0, 0)` | `(500, 0, 0)` | ✓ |
| V2 (rot `eulerAngles 90 0 0`, pos `(0, 500, 0)`) | `(0, 500, 0)` | `(0, 500, 0)` | ✓ |
| V3 (rot `eulerAngles 0 0 45`, pos `(300, 400, 0)`) | `(300, 400, 0)` | `(300, 400, 0)` | ✓ |

In every case (whether identity, asymmetric rotation, or both), the
encoded translation row equals the Max-side world position. Same for
rotation rows. None of the three Phase 14c candidate hypotheses
(stray `Inverse(R)` / column-vector convention / IGame BoneSys
quirk) survives the variant data — they all predicted at least one
case where encoded ≠ Max, but no such case appeared.

## Decision and follow-up

- **No walker change.** Phase 14b's `walk_node` static-mesh-parent
  inheritance is correct as-is.
- **Synthetic fixture fixed.** [`test_phase14b_static_mesh_parent_bone.ms`](../../tests/maxscript/test_phase14b_static_mesh_parent_bone.ms)
  + [`verify_test_phase14b_static_mesh_parent_bone.py`](../../tests/maxscript/verify/verify_test_phase14b_static_mesh_parent_bone.py)
  updated to use a non-orbiting createBone direction and assert the
  honest bind position (HandBone `(500, 0, 0)`, GunMesh parent-local
  `(0, 200, 0)`).
- **Documentation added.** `docs/build.md` "Authoring convention —
  bind pose lives at frame 0" section captures both pitfalls so
  future authors and future Claude sessions can recognise them.
- **EI_SNOWTROOPER specifically.** The `.max` file's bind pose at
  frame 0 is degenerate by content design. The user has options:
  re-author the gun's frame-0 position keyframe to match the in-hand
  position, OR shift the animation range to start at 0. No walker
  workaround is appropriate — every other test in the corpus
  authors `animationRange = (interval 0 N)` and would silently
  break if the walker changed its sampling frame.

## Diagnostic tooling produced (kept committed for future use)

- [`tests/maxscript/_phase14d_variants.ms`](../../tests/maxscript/_phase14d_variants.ms) —
  four-variant single-bone export + Max-side TM dump. Discriminates
  walker bugs from fixture authoring bugs.
- [`tests/maxscript/_phase14d_snowtrooper_dump.ms`](../../tests/maxscript/_phase14d_snowtrooper_dump.ms) —
  loads EI_SNOWTROOPER, dumps key bone TMs at explicit `at time 0`
  + exports for chain-dump comparison. Pattern is portable to any
  user-content scene that exhibits a similar bind-pose mismatch.

## Related

- Issue: [#87](https://github.com/DrKnickers/max2alamo-2026/issues/87)
  (Phase 14d).
- Predecessor: [`phase-14c-gun-misaligned-findings.md`](phase-14c-gun-misaligned-findings.md)
  (the investigation that introduced the false-positive convention-bug
  hypothesis).
- Original bug: [#84](https://github.com/DrKnickers/max2alamo-2026/issues/84)
  (resolved by Phase 14a quat conjugate + Phase 14b static-mesh
  parent inheritance — both still correct).

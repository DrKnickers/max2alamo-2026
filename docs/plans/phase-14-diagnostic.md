# Phase 14 diagnostic plan — animation playback pose mismatch

> **Status**: **RESOLVED 2026-05-15.** Root cause was Max SDK's
> `Quat(Matrix3)` constructor returning the inverse rotation
> quaternion (the conjugate of what the `.ala` format expects).
> Fix in [scene_walker.cpp:1582](../../max2alamo/src/scene_walker.cpp#L1582):
> conjugate the result of `extract_rotation_quat` before packing.
> One-line change; .alo bone matrices unchanged (rest poses unaffected);
> .ala per-frame quat values now byte-match vanilla content (15 of
> 18 shared bones in EI_SNOWTROOPER's IDLE_00 match vanilla
> EI_TROOPER's IDLE_00 within epsilon; the 3 outliers are real
> asset-authoring differences between the ThrREv mod's Snowtrooper
> rig and vanilla's stock Trooper rig).
>
> See [issue #84](https://github.com/DrKnickers/max2alamo-2026/issues/84)
> for the original bug report.

## Resolution diagnostic (the path that actually worked)

The story from this session, in the order findings arrived:

1. **The issue body's plan (byte-diff vanilla via Mike Lankamp's importer)
   isn't runnable** — Mike's importer doesn't support Max 2026.
2. **A minimal MaxScript reproducer (2-bone chain with keyframed Z
   rotation)** was authored, but BoneSys parenting in MaxScript does
   *not* preserve world TM by default — `bChild.parent = bParent`
   silently moves the child's world position through the parent's
   rotation matrix. The minimal repro exhibited a "wrong pose" symptom
   that came from this Max-side authoring quirk, not from a walker
   bug. Time was spent chasing a false H1/H6/H7 hypothesis before
   recognising this.
3. **The actual reproducer was EI_SNOWTROOPER** from the start (per
   the issue body, and what the user had loaded). Exporting it via
   `3dsmaxbatch` and dumping a representative clip (`IDLE_00.ala`)
   with `ala_diff --dump`, then **side-by-side comparing against
   vanilla `EI_TROOPER_IDLE_00.ALA`** (different asset, similar rig,
   in the corpus) — that surfaced the bug in a single side-by-side:

   | Field | Vanilla | Ours (pre-fix) |
   |---|---|---|
   | `trans_offset` | `(-0.430665, 0.235189, 4.661334)` | `(-0.430665, 0.235189, 4.661334)` ✓ |
   | `default_rot` (x, y, z, w) | `(+0.478, -0.523, +0.454, +0.540)` | `(-0.478, +0.523, -0.454, +0.540)` |

   Same magnitudes, but x/y/z signs flipped and w unchanged — the
   textbook **quaternion conjugate** signature, representing the
   inverse rotation.
4. **Root cause**: Max SDK's `Quat(Matrix3)` constructor returns the
   conjugate of the rotation the `.ala` format wants. The `0x206` 4x3
   bone matrix doesn't go through quat extraction, so rest poses were
   correct (matching the issue body's "rest pose looks correct"
   observation). Animation tracks DO go through quat extraction, so
   every frame of every bone was being written as the inverse — chain
   composition through 36 bones made it appear as "rotated 90 degrees
   forward but not properly."
5. **Fix**: conjugate the quat in `extract_rotation_quat` before
   returning. `Quat(-q.x, -q.y, -q.z, q.w)`. Single line of code.
   Confirmed: post-fix B_Pelvis matches vanilla byte-for-byte.

## Hypotheses ruled out along the way

In order of investigation:

| # | Hypothesis | Verdict | Why |
|---|---|---|---|
| H1 | `compose_with_object_offset` applied to per-frame data when it should only apply to the static `0x206` matrix | ❌ | No-op for BoneSys bones (object offset is identity); confirmed in [scene_walker.cpp:820](../../max2alamo/src/scene_walker.cpp#L820). Issue body's "compose is self-consistent" was correct after all. |
| H6 | The static `0x206` bone matrix encoding has a basis swap on parented bones | ❌ | The walker's INode-based parent-local computation produces identical bytes to IGame's `GetLocalTM()`; both correctly encode what Max contains. The "wrong" rest pose was a Max-side authoring quirk in the minimal repro. |
| H7 | `IGameNode::GetLocalTM` uses a different convention than MaxScript's `node.transform` | ❌ | Both produce the same bytes for BoneSys bones. False positive triggered by misreading MaxScript's `node.transform` semantics for parented nodes. |

The bug was in `Quat(Matrix3)`, not in any of the matrix-level paths.

## Phase 14b — static-mesh attachment to Max-parent bone (also in PR #86)

After Phase 14a fixed the per-frame quat extraction, EI_SNOWTROOPER's
body played correctly but the gun stayed in its bind-pose world
position while the arm animated — visible in AloViewer as the rifle
floating free of the hand. Root cause: our walker's static-mesh path
hardcoded `parent_index = 0` and encoded the mesh's WORLD TM, so the
mesh attached to scene Root regardless of its Max-side parent.

Fix in [scene_walker.cpp:1251 (`!is_skinned` branch)](../../max2alamo/src/scene_walker.cpp#L1251):
look up the Max parent INode in `bone_map`. If found, parent the
synthetic per-mesh bone to it and encode `mesh_world * Inverse(parent_world)`
(parent-local TM). Otherwise keep the legacy world-to-Root path.

Test: `tests/maxscript/test_phase14b_static_mesh_parent_bone.ms` + its
`verify_*.py` sibling. The verifier asserts the GunMesh's synthetic
bone has `parent_index = 1` (HandBone), not `0` (Root), and that its
translation row is `(200, 0, 0)` in HandBone-local space, not world.

Data-layer verification on EI_SNOWTROOPER (the actual #84 reproducer):
after the fix, the gun mesh's synthetic attachment bone (`Box01`)
parents to `B_Gun` (bone index 15), which chains up through
`B_Hand_R → B_FArm_R → B_Bicep_R → B_Chest → Turret_00 → B_Pelvis →
Root`. Similarly the muzzle-flash attachment bone (`MuzzleA_00_Flash`)
now parents to `MuzzleA_00`. The collision-hull bone (`Col_Main`)
legitimately stays at Root since it's a whole-figure proxy.

Manual verification (pending DrKnickers in AloViewer): re-export of
EI_SNOWTROOPER should now play animations with the gun rigidly
attached to the hand. Rest pose unchanged from Phase 14a state.

## Follow-up work for future sessions

- **Regenerate Phase 8 maxbatch SHA tests.** All
  `tests/maxscript/test_phase8_*` (and any rotation-keyframe test
  that fixed-string-compares against the .ala bytes) need their gold
  SHAs / expected values regenerated. These were validated against
  the bug-producing walker so they all break after this fix. New
  values are correct.
- **Add an AloViewer-playback verification gate** to close the Tier 4
  coverage hole. Phase 8's tests asserted the .ala bytes match a
  fixed SHA, but never asserted the BYTES MATCH WHAT ALOVIEWER
  EXPECTS. A side-by-side dump-vs-vanilla check at fix time would
  have caught this immediately; that's what `ala_diff` is now for.

## Why the issue's original plan is unrunnable

The issue body proposes: open a vanilla `.alo`+`.ala` pair via Mike
Lankamp's importer in Max, re-export through our walker, byte-diff the
two `.ala` files. Two blockers:

1. **Mike's importer doesn't work in Max 2026.** It targets max9 and
   has known compatibility breaks; bringing it forward is its own
   sub-project.
2. **No scene-reconstruction path exists.** Without an importer, there
   is no way to recreate a vanilla scene in Max well enough to make
   our walker emit something comparable to vanilla bytes.

Even if blocker 1 were fixed, a third issue lurks: a **raw byte diff
is dominated by legitimate noise**. Per-bone `trans_offset` /
`trans_scale` recompute from per-clip min/max, so any sampling-
precision delta cascades into different bytes across the entire
pool. Semantic comparison is required.

## What landed in this session

### `tools/ala_diff/` — typed `.ala` comparator

Parses both files through `read_ala` and compares the typed
`AlaAnimation` view, not the raw bytes:

```text
ala_diff --dump <file.ala>           human-readable typed dump
ala_diff <a.ala> <b.ala>             semantic diff (bones matched by name)
```

Categories compared, in this order:

1. **Header** — `n_frames`, `fps`, `n_bones`, `is_foc`, pool sizes.
2. **Bone presence** — which names exist in one file but not the other.
3. **Bone metadata** — `skeleton_index`, `idx_rotation`,
   `idx_translation`, `default_rotation` (unpacked, sign-aligned).
4. **Per-frame rotation** — unpacked `Quat4`, sign-aligned via dot
   product, epsilon `1e-4` (~3 LSB at 1/32767 scale).
5. **Per-frame translation** — unpacked `(x, y, z)`, epsilon `1e-3`
   scene units.

`trans_offset` / `trans_scale` mismatches are reported but flagged
informational — different ranges of motion legitimately produce
different packing parameters; the unpacked positions are the source
of truth.

First 5 divergences per category are shown; rest are counted. Exit 0
iff equivalent within epsilon.

### `tests/maxscript/test_phase14_min_chain.ms` — minimal reproducer

Two-bone parent-child chain plus one 11-frame keyframed Z-axis
rotation on the child. The smallest viable scene that can exhibit
parent-relative transform bugs. Predicted output bytes are in the
script header comment and are short enough to verify by eye via
`ala_diff --dump`.

## Autonomous findings before any Max-side work

Running `ala_diff --dump` against the vanilla FoC corpus surfaces
two structural observations the issue's "byte-diff" plan would have
missed:

### Finding 1: our walker emits translation tracks too aggressively

Vanilla FoC walk cycles emit translation tracks for only the bones
that actually translate:

| Vanilla file | `n_translation_words` | Translating bones |
|---|---|---|
| `EI_TROOPER_WALKMOVE_00.ALA` | 3 | 1 (just the root/pelvis) |
| `EI_ROYALGAURD_WALKMOVE_00.ALA` | 3 | 1 |
| `EI_DARKTROOPER_ONE_WALKMOVE_00.ALA` | 15 | 5 |
| `AI_AIRWHALE_IDLE_00.ALA` | 0 | 0 (idle, no motion) |

Our walker
([`scene_walker.cpp:1657`](../../max2alamo/src/scene_walker.cpp#L1657))
assigns `idx_translation` to **every animatable bone**, regardless
of whether it actually moves. For Snowtrooper (36 animatable bones)
we emit ~108 translation words per clip where vanilla would emit
3-15.

This is a deviation from vanilla convention, but it is **not
automatically a bug**: AloViewer reads `pos = uint16[3] *
trans_scale + trans_offset` per frame and uses it as the bone's
local translation. If our packed translation is correct (= the
bone's local-to-parent translation), playback should be visually
identical to vanilla regardless of how many bones have tracks.

The question is whether our packed translation **is** correct. Which
brings us to:

### Finding 2: `compose_with_object_offset` is applied to per-frame translation

[`scene_walker.cpp:1747`](../../max2alamo/src/scene_walker.cpp#L1747)
samples the bone's per-frame local translation as
`compose_with_object_offset(node, GetLocalTM(t)).GetRow(3)`. This
pre-multiplies the bone's local TM by its object offset (rotation
+ position).

The same `compose_with_object_offset` is correctly applied to the
static `0x206` bone matrix
([`scene_walker.cpp:938`](../../max2alamo/src/scene_walker.cpp#L938)).
The issue body lists this as a refutation: *"Object offset compose
applied differently to rest vs animation — NO. Same compose function
called in both paths. Self-consistent."*

But "self-consistent" is only a refutation **if AloViewer consumes
the two values identically**. If AloViewer uses the `0x206` matrix
as a vertex-skinning bind pose and the `.ala` translation as a
*per-frame replacement* of the bone's local TM, the two are not
parallel uses — and applying object offset to both could double-
apply it at runtime.

This is the **primary new hypothesis**. The minimal repro is
designed to falsify it: on a 2-bone chain with no skinned mesh
beyond a tiny anchor cylinder, the bug should manifest as a
visible swing-direction or pivot-location error in AloViewer if
the hypothesis is correct.

### Finding 3 (cleared, not a bug)

`default_rotation` in vanilla FoC matches frame 0 of the rotation
track exactly — i.e. vanilla also writes the rest-pose quat into
mini-chunk 17, not identity. Our walker
([`scene_walker.cpp:1684`](../../max2alamo/src/scene_walker.cpp#L1684))
already does this. The "double-baked rest pose" hypothesis is
cleared.

(EaW vanilla files have `default_rotation = identity` because EaW
format does not use the file-scope pool indices; the field is dead
data there. Both findings are consistent.)

## The Max-side workflow

Step 0 is the gating step. Steps 1-5 run in order.

### Step 0 — confirm the harness builds

```pwsh
cmake -B build -S . -DBUILD_TESTING=OFF
cmake --build build --config Release --target ala_diff
```

Expected: `build/tools/ala_diff/Release/ala_diff.exe` exists.

Self-diff sanity check (vanilla file vs itself):

```pwsh
build\tools\ala_diff\Release\ala_diff.exe `
  tests\corpus\ala-eaw\AI_DIANOGA_IDLE_00.ALA `
  tests\corpus\ala-eaw\AI_DIANOGA_IDLE_00.ALA
```

Expected: `EQUIVALENT (within epsilon)`.

### Step 1 — export the minimal scene

Run the new test fixture through `3dsmaxbatch.exe` (no GUI needed):

```pwsh
"C:\Program Files\Autodesk\3ds Max 2026\3dsmaxbatch.exe" `
  tests\maxscript\test_phase14_min_chain.ms `
  -listenerlog build\maxbatch\test_phase14_min_chain.log -v 2
```

Output: `build/maxbatch/test_phase14_min_chain.alo` plus a sibling
`test_phase14_min_chain.ala` (the multi-clip suffix is empty for
the un-suffixed back-compat path — see `walk_animations`).

### Step 2 — dump the `.ala` and verify predicted bytes

```pwsh
build\tools\ala_diff\Release\ala_diff.exe --dump `
  build\maxbatch\test_phase14_min_chain.ala
```

Expected from the script header:

- `n_frames = 11`, `fps = 30`, `n_bones = 3`, `is_foc = true`
- `n_rotation_words = 8` (4 each for ParentBone + ChildBone; Root
  is synthetic, no track)
- `n_translation_words = 6`
- ParentBone rotation: identity at frame 0 / mid / last.
- ChildBone rotation: identity at frame 0, ~`(0, 0, 0.3827, 0.9239)`
  at frame 5, ~`(0, 0, 0.7071, 0.7071)` at frame 10.
- ChildBone translation: `(0, 20, 0)` (parent-local pivot) at every
  frame — equal across all 11 frames if the static pivot is exported
  correctly.

If the dump deviates here, the bug is in the walker's extraction,
not in AloViewer's interpretation. Proceed to Step 5 with that
finding.

### Step 3 — visual playback in AloViewer

Load the `.alo` + `.ala` pair in AloViewer. Frame the camera so the
2-bone chain is centered. Step through frames 0 / 5 / 10:

- **Frame 0**: ChildBone points straight along +Y from its joint at
  (0, 20, 0). Anchor cylinder hangs along the bone. Expected: clean
  "L" shape with the parent vertical and the child vertical above it.
- **Frame 5**: ChildBone rotated 45° about its own Z. The cylinder
  swings 45° from its starting orientation.
- **Frame 10**: ChildBone rotated 90°. Cylinder now perpendicular
  to the parent.

### Step 4 — observe, classify the symptom

Four buckets:

#### 4a. Plays correctly

The 2-bone chain is too simple to trigger the bug. Increment
complexity one variable at a time:

- (i) Add an object offset to ParentBone (Affect Pivot Only, rotate
  pivot 90° about X) — does this trigger it? If yes: bug is in
  object-offset compose interacting with parent-relative chain.
- (ii) Add a 3rd grandchild bone, keyframe rotation on it too — does
  this trigger it? If yes: bug is in chained parent-relative transforms.
- (iii) Add translation keyframes on the child — does this trigger it?
  If yes: bug is in per-frame translation pool, not rotation.

Each "yes" pins the bug class. Each "no" advances to the next
variable.

#### 4b. Plays wrong in a recognisable way

The visible deviation tells us which component is wrong:

- **Child swings around the wrong pivot** (e.g. around its own tip
  instead of its base) → translation track is wrong; check Finding
  2 hypothesis.
- **Child swings in the wrong direction** (e.g. -Z instead of +Z)
  → quaternion sign or axis-order bug.
- **Child rotates but parent inherits the rotation** (parent visibly
  rotates too) → pool-layout direction mismatch (frame-major vs
  bone-major encoding).
- **Both bones tangled, model lies flat** (the Snowtrooper symptom)
  → object-offset double-application, the primary Finding-2
  hypothesis.

#### 4c. Plays wrong but pattern is unclear

Run AloViewer with the skeleton visualization on (View → Skeleton).
Compare the bone-joint positions to the expected positions at frame
0, 5, 10. If even frame 0 is wrong, the bug is in the bind pose or
default rotation handling. If frame 0 is right but frame 5/10 wrong,
the bug is in animation playback.

#### 4d. Reproduces Snowtrooper's exact symptom

"Rotated 90 degrees forward, but not properly" on a 2-bone chain
proves the bug is in fundamental convention, not in any
complex-rig-specific code. Bisect by toggling, in the walker, one
variable at a time:

- (i) Remove `compose_with_object_offset` from the per-frame call
  at `scene_walker.cpp:1723` (leave the rest-pose call at 1682 alone)
  — re-export, re-check. If the bug disappears: confirmed. The fix
  is to apply object offset only to the static 0x206 matrix, not
  the per-frame translation/rotation.
- (ii) If (i) does not fix it: swap quat component packing order
  in `pack_quat_int16` to test XYZW vs WXYZ.
- (iii) If (ii) does not fix it: change pool indexing to bone-major
  (`base = idx_rotation * n_frames + f*4 + c`) instead of frame-major.

Each toggle is a temporary patch — do not commit. Once the fix is
identified, the proper change goes through TDD with a synthetic
unit test that hand-computes the expected bytes.

### Step 5 — record the finding

Append the result to this doc under a "Resolved by" section.
Update `docs/development-log.md` Phase 14 row when the actual fix
lands.

## Hypothesis ranking (priors after Findings 1-3)

| # | Hypothesis | Prior | Why |
|---|---|---|---|
| H1 | `compose_with_object_offset` applied to per-frame translation/rotation when it should only be applied to the static `0x206` matrix | **High** | Finding 2; consistent with "self-consistent compose" being misclassified as a refutation in the issue body; BoneSys bones (Snowtrooper's authoring) have non-trivial object offsets by construction. |
| H2 | Pool indexing direction (frame-major vs bone-major) mismatched between writer and engine reader | Medium | `ala_typed_roundtrip` cannot catch this — read+write is symmetric. Our walker is the only place a fresh pool is laid out; vanilla parity is asserted but not proven. |
| H3 | Quaternion component order (XYZW vs WXYZ) wrong in pack | Low | Already specified XYZW per `alamo2max.ms:727-736` and confirmed in format-notes.md:534. Synthetic single-bone tests would have caught a swap. |
| H4 | Parent-relative basis mismatch — `IGameNode::GetLocalTM(t)` returns something different from what AloViewer expects | Medium | Plausible for biped/BoneSys mix, but Snowtrooper is pure BoneSys (no biped). |

H1 is the cheap-to-test hypothesis: one-line change in the walker,
re-export the minimal scene, look at AloViewer. If wrong, we have
not lost much.

## Out of scope for this diagnostic session

- Updating Mike Lankamp's `alamo2max.ms` for Max 2026. Separate
  project; not needed once the minimal-reproducer path lands.
- Adapting `n_translation_words` to vanilla's "only emit for moving
  bones" convention. Finding 1 is a deviation but not a known
  correctness bug. Address as a separate phase if Phase 14's fix
  doesn't already make our output indistinguishable in playback.
- Patching AloViewer to log read values. Available as a fallback if
  the bisect in Step 4d doesn't converge, but heavy enough that the
  minimal-scene approach should be tried first.

## Verification of the fix (when found)

When a candidate fix lands in `scene_walker.cpp`:

1. The minimal repro plays the expected 2-bone Z-rotation correctly
   in AloViewer.
2. EI_SNOWTROOPER (the original reproducer) plays its `.ala` clips
   in the same poses Max shows at the same frame.
3. The full vanilla `.ala` corpus still round-trips byte-identically
   via `ala_typed_roundtrip` (the fix touches the walker's encoder,
   not the typed read/write pipeline).
4. The synthetic Phase 8 SHA tests at `tests/maxscript/test_phase8_*`
   either keep their existing SHA, or are updated to the corrected
   value with a new test asserting the correct AloViewer playback.

Item 4 is the **explicit hole** in Phase 8 that allowed this bug to
slip. The Phase 8 tests check bytes against a fixed SHA but never
checked AloViewer playback. The fix PR should add at least one
"AloViewer-renders-this-pose-correctly" gate (manual or automated)
to close the Tier 4 coverage gap.

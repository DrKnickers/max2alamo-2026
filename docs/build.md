# Build instructions

## What gets built where

| Target | Built in | Notes |
|---|---|---|
| `alamo_format` static lib | CMake / CI | Pure C++17, no Max SDK dependency |
| `alo_dump` CLI | CMake / CI | Standalone tool |
| `alo_roundtrip` CLI | CMake / CI | Standalone tool |
| `max2alamo.dle` | CMake (local only) | Requires 3ds Max 2026 SDK; not redistributable, hence not in CI |

## Prerequisites

- **Windows 10 / 11** (the Max plugin is Windows-only).
- **Visual Studio 2022 or 2026** with the "Desktop development with C++" workload (MSVC v143 or v144).
- **CMake 3.20+** (ships with VS, or install separately via `winget install Kitware.CMake`).
- For the Max plugin: **3ds Max 2026** + **3ds Max 2026 SDK** (free with Max install).

## First-time clone setup

Run once after `git clone`:

```powershell
powershell -File scripts/setup-git-hooks.ps1
```

This sets `core.hooksPath = .githooks` so the in-repo `pre-commit` hook fires on every `git commit` in this clone. Currently the hook rejects commits that would put the maintainer's pre-v0.9.1 private email back into the public history (the repo's history was rewritten + force-pushed in the v0.9.1 release window to scrub that address; see `.githooks/pre-commit` for the full rule). Override with `--no-verify` if you have a deliberate reason.

You also want to set the repo-local commit identity to your own GitHub noreply alias, so your private address doesn't end up in `git log`:

```bash
git config user.email "<your-github-id>+<your-username>@users.noreply.github.com"
git config user.name  "<your-github-username>"
```

(GitHub auto-provides this noreply email — see `https://github.com/settings/emails` to find yours.)

## Building the format library + CLI tools

From the repo root:

```bash
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Outputs:
- `build/alamo_format/Release/alamo_format.lib`
- `build/tools/alo_dump/Release/alo_dump.exe`
- `build/tools/alo_roundtrip/Release/alo_roundtrip.exe`

If the Max SDK is also installed at one of the autodetected paths (`C:\Program Files\Autodesk\3ds Max 2026 SDK\maxsdk` or `D:\Autodesk\3ds Max 2026 SDK\maxsdk`), `max2alamo.dle` is built too.

## Building only the Max plugin

The plugin target is configured automatically when the Max SDK is found. If the SDK lives somewhere non-standard, point CMake at it:

```bash
cmake -B build -S . -DMAX_SDK_DIR="D:/path/to/maxsdk"
cmake --build build --config Release --target max2alamo
```

Output: `build/max2alamo/Release/max2alamo.dle` (~ a few hundred KB).

## Installing the plugin into 3ds Max 2026

Every build copies the `.dle` into a user-writable folder inside the repo:

```
<repo>/plugin/max2alamo.dle
```

The copy is done by a CMake `POST_BUILD` step, so `cmake --build build --config Release --target max2alamo` always leaves a fresh `.dle` at that exact path. The folder is gitignored.

**One-time Max setup** so Max scans that folder on startup:

1. Open 3ds Max → **Customize** → **Configure System Paths…**
2. Switch to the **3rd Party Plug-Ins** tab.
3. **Add…** the absolute path to the repo's `plugin/` folder (e.g. `C:\Modding\max2alamo-2026\plugin\`).
4. OK out, then restart Max.

From then on the dev loop is `build → restart Max`. No copy step, no UAC prompt.

If you previously installed the plugin into `C:\Program Files\Autodesk\3ds Max 2026\Plugins\`, **delete it** (one final elevation) so Max doesn't try to load the plugin from two places — the second load fails with a duplicate-Class_ID error in `Network\Max.log` and is silent in the UI.

If you'd rather have the post-build copy go somewhere else (e.g. directly into the Plugins folder, or a custom shared location), pass `-DMAX2ALAMO_INSTALL_DIR=<absolute path>` at configure time.

If Max fails to load the plugin, check `<MAX>\Network\Max.log`. Most common cause: the `.dle` was built against the wrong SDK version (must match Max 2026, not 2024 / 2025 / etc.).

## Building everything in one go

```bash
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
```

This produces the static lib, both CLIs, the `.dle` (if SDK present), and the test executable.

## CI

GitHub Actions builds only the SDK-independent targets (`alamo_format`, `alo_dump`, `alo_roundtrip`, tests). The Max plugin is built locally by contributors and attached to GitHub Releases as a downloadable artifact at version tags.

## Testing the export pipeline

Every plugin / walker / format-library change must pass the full export test pyramid before merge. Tiers 1–4 are automated by `scripts/run-max-tests.ps1`; Tier 5 is a manual checklist for things batch mode can't verify.

### Tier 1 — Tier-1 invariants (automated, two modes)

`tests/maxscript/verify/validate_alo.py` runs on every exported `.alo`. Two modes calibrated against different ground truths:

- **Strict mode (default for harness exports)** — adds checks our walker output is supposed to satisfy: sub-1e-3 normal/tangent length, tangent perpendicularity `|dot(T,N)|<=0.15`, per-vertex weights sum to 1.0, non-negative weights. Vanilla content routinely violates these; our pipeline never does, so a violation in our output is a real regression.
- **Loose mode** (`--loose`) — structural baseline only: bone parent topology, index/vertex consistency, name/billboard ranges, connection count = `(meshes + lights)`. Calibrated against the entire vanilla EaW+FoC corpus (2066/2066 files pass).

Run `python scripts/sweep_corpus_validator.py` to confirm the loose-mode validator stays vanilla-compatible after walker / format changes. Every file in `tests/corpus/` should pass; if a vanilla file fails, the validator is too strict.

### Tier 2 — Writer round-trip (automated)

Each exported `.alo` is fed through `build\tools\alo_roundtrip\Release\alo_roundtrip.exe`. Non-zero exit means the writer's re-serialisation diverges from the original bytes — i.e. a regression in the format-library writer. Catches drift for free without per-test boilerplate.

### Tier 3 — Feature-specific verifiers (automated)

Each `tests/maxscript/test_*.ms` ships with a paired `verify_test_*.py` that asserts the behaviour the test was written to pin. When you add a feature, add its Tier 3 verifier.

### Tier 4 — Playback validator (automated)

`tests/maxscript/verify/validate_playback.py` runs on every exported `.alo` (plus any sibling `.ala` companions) after Tier 3. Asserts vanilla-empirical conventions the engine reads at playback time that aren't caught by structural validity (Tier 1), writer round-trip (Tier 2), or feature-specific (Tier 3) tests. Three concrete checks:

- **Phase 14e (translation)** — for every bone in the `.ala` with no per-frame tracks (`idx_translation = idx_rotation = -1`), assert `trans_offset` matches the `.alo` bind matrix translation row within 1e-3.  Catches the regression class where the walker leaves `trans_offset` at struct zero for synth bones (static-mesh attachment bones, light synth, proxy synth, light targets) — engine reads "bone is at parent-local (0,0,0)" the moment any clip starts playing and snaps the bone to the parent's origin.
- **Phase 14e (rotation, loose)** — for the same bones, if the bind matrix's rotation is non-identity, `default_rotation` must NOT be the struct-zero identity quat `(0, 0, 0, ±32767)`.  Catches "walker forgot to extract + pack the bind rotation into `default_rotation`".  Loose form; doesn't catch the full Phase 14a quat-sign convention mismatch (that requires a Max-equivalent matrix-to-quat implementation in Python, deferred).
- **Phase 10.5** — every submesh whose `vertex_format_name` contains `RSkin` or `B4I4` must have a `0x10006` bone-remap chunk (`Submesh.bone_remap` non-`None`).  Catches the regression class where the walker emits skinned geometry without the local-to-global slot remap the engine's renderer dereferences — produces stretched-triangle artifacts when present.

```powershell
# Tiers 1-4 in one shot:
powershell -File scripts/run-max-tests.ps1
```

When this tier flags a regression, the error message names the bone / submesh, the bad value vs the expected, and the walker code path most likely to have caused it.  See `tests/maxscript/verify/validate_playback.py` docstring for the full convention list + regression-class background.

### Tier 5 — Manual smoke tests (release / major-PR sign-off)

Things batch mode can't verify. Run these for releases or whenever a PR's behaviour can't be inferred from on-disk structure alone:

- [ ] **Mike Lankamp's `alamo2max` MAXScript importer.** Open the exported `.alo` in his importer (any supported Max version). No errors in the MAXScript listener; scene reconstructs with the expected mesh / bone / material set.
- [ ] **AloViewer.** Drag the exported `.alo` onto AloViewer. Renders without crash; geometry has expected shape and orientation; materials visible (textured surfaces show texture, not solid colour).
- [ ] **In-game (EaW / FoC).** Drop the `.alo` into a mod folder and load the relevant unit. Loads without crash; renders correctly; for animation work, animations play; hardpoints fire from expected positions.

### Tier 5 — Animation Settings rollout (Phase 11b.2)

The Utility-panel Animation Settings rollout drives multi-clip authoring + per-clip `.ala` emission. Catch2 covers the pure-C++ logic (parsing, validation, prev/next, range union — 43 cases under `alamo_format/tests/anim_clip_list_test.cpp`); these manual steps cover the Win32 dispatch, undo semantics, and animationRange propagation that no headless harness can exercise. Run them in 3ds Max 2026 after rebuilding the `.dle`.

1. **Open Max with an empty scene.** Open Utilities → More → Alamo Utility. Verify: Animation Settings rollout present; combo shows `-- none --`; all controls except Add disabled.
2. **Click Add → type `WALK` → OK.** Verify: combo now shows `WALK`; Start spinner = 0; End spinner = `animationRange.end` (Max default 100); time-slider bounds = [0, 100]; MAXScript Listener `getUserProp rootNode "Alamo_Anim_Clips"` returns `"WALK"`; `Alamo_Anim_WALK_Start` = 0; `Alamo_Anim_WALK_End` = 100.
3. **Edit End spinner to 30.** Verify: spinner shows 30; time-slider bounds = [0, 30]; `Alamo_Anim_WALK_End` = 30; Edit menu has one new undo entry "Edit Clip End Frame"; `Ctrl-Z` restores spinner to 100, time-slider to [0, 100], prop to 100; then `Ctrl-Y` (redo) restores spinner to 30, time-slider to [0, 30], prop to 30 (Phase 11b.2.2: `NOTIFY_SCENE_UNDO` / `NOTIFY_SCENE_REDO` repaint the rollout).
4. **Click Add → type `ATTACK` → OK.** Verify: combo selects `ATTACK`; Start=0, End=30 (current `animationRange.end`); `Alamo_Anim_Clips` = `"WALK|ATTACK"`.
5. **Click `<<`.** Verify: combo flips to `WALK`; Start/End spinners load `WALK`'s stored 0 / 30; **time-slider scrubs to [0, 30]** (the user-requested combo-scrub-timeline behavior).
6. **Click `>>`.** Verify: combo flips back to `ATTACK`; spinners load 0 / 30; time-slider scrubs.
7. **Manually drag the time-slider's right edge to frame 60.** Verify: `animationRange` is now [0, 60] but `Alamo_Anim_ATTACK_End` is still 30 (editor model: manual scrubs are preview-only, don't mutate clip data).
8. **Click `Display Current`.** Verify: time-slider snaps back to [0, 30] (the stored ATTACK range).
9. **Click `Display All`.** Verify: time-slider = [0, 30] (the union of `WALK` [0, 30] + `ATTACK` [0, 30]). Edit ATTACK's End to 90 and click Display All again → time-slider = [0, 90].
10. **Try Add → type `walk` (lowercase) → OK.** Verify: inline validator error appears ("Clip names use uppercase A-Z, digits 0-9, and underscores"); the prompt stays open; combo unchanged.
11. **Try Add → type `WALK` (duplicate) → OK.** Verify: inline error "A clip with that name already exists"; prompt stays open.
12. **Click `Del` with `ATTACK` selected.** Verify: confirmation `MessageBox` ("Delete animation clip \"ATTACK\"?"); click Yes; combo selects `WALK` (the previous clip); `Alamo_Anim_Clips` = `"WALK"`; `Alamo_Anim_ATTACK_Start` = -1 (soft-delete sentinel — Phase 10b walker contract treats this as absent identically to true removal).
13. **File → Save the scene to a temp `.max`. File → New (empty scene). File → Open the temp `.max`.** Verify: rollout combo repopulates with `WALK` after the open (the `NOTIFY_FILE_POST_OPEN` hook ran); spinners load 0 / 30; time-slider scrubs to [0, 30].
14. **Load a single-clip back-compat scene** (un-suffixed `Alamo_Anim_Start` / `_End` / `_Name` only, no `Alamo_Anim_Clips`). Verify: combo shows the un-suffixed clip name; spinners load its range. Click `Add` → upgrades to multi-clip (sets `Alamo_Anim_Clips`; un-suffixed props remain intact — Phase 11b.1's precedence rule shadows them, which is the right behavior).
15. **Export the scene from step 9.** Verify: per-clip `.ala` siblings emit (`<basename>_WALK.ala`, `<basename>_ATTACK.ala`); `.export.log` shows per-clip lines (Phase 11b.1's coverage). Round-trip the `.ala` files through Mike Lankamp's importer (Tier 4 cross-tool above).

**Negative tripwire G** (manual sanity check during development): in `max2alamo/src/animation_settings_dlg.cpp::ApplySelectedClip`, comment out the `SetAnimRangeAndNotify(util->m_ip, start, end)` line. Rebuild. Run step 5 above. Expected: spinners flip to `WALK`'s range but time-slider does NOT scrub. Confirms the smoke test would catch a regression of the user-requested "combo selection scrubs timeline" behavior.

### Tier 5 — Legacy `.max` clip-data import (Phase 11c)

The legacy Petroglyph max2alamo.dle Utility plugin (Max 6/8/9, EaW/FoC/UaW eras) stored per-clip animation metadata as 342-byte `appData` records keyed off `Class_ID(0x70a24090, 0x60c90f03)`. Phase 11c's `NOTIFY_FILE_POST_OPEN` hook translates those records into Phase 11b's `Alamo_Anim_Clips` + `Alamo_Anim_<NAME>_Start/_End` user-prop convention so the Animation Settings rollout surfaces the imported clips without user action.

Catch2 covers the pure-C++ scanner (14 cases under `alamo_format/tests/legacy_clip_scan_test.cpp`); these manual steps cover the Max-SDK glue (file IO + user-prop write + animationRange extend) plus the cross-tool round-trip that no headless harness reaches.

Fixtures used (local-only, under `tests/corpus/legacy/ThrREv/Ascendancy/`): `Super Battle Droid/CIS_SBD.max` (8 clips, 400 frames), `Snowtrooper/EI_SNOWTROOPER.max` (60 clips, 2488 frames), `Stormtrooper/Stormtrooper.max` (60 clips, 2488 frames — identical clip set to EI_SNOWTROOPER).

1. **Open Max with an empty scene.** Open Utilities → More → Alamo Utility. Verify: Animation Settings rollout combo shows `-- none --`.
2. **File → Open → `CIS_SBD.max`.** Verify: MAXScript Listener shows `max2alamo: imported 8 legacy animation clip(s) from <path>`; rollout combo populates with 8 entries (MOVE_00 first); selecting any clip loads its `Start`/`End` into the spinners. MAXScript: `getUserProp rootNode "Alamo_Anim_Clips"` returns `"MOVE_00|IDLE_00|IDLE_01|TRANSITION_00|ATTACK_00|ATTACKIDLE_00|DIE_00|DIE_01"`; `getUserProp rootNode "Alamo_Anim_MOVE_00_Start"` returns `1`; `Alamo_Anim_DIE_01_End` returns `400`; Max's `animationRange` covers `[0, 400]` (extended from the 100-frame default).
3. **Click `Display All`.** Verify: time-slider snaps to `[1, 400]` (the union of all 8 clip ranges).
4. **File → Open → `EI_SNOWTROOPER.max`.** Verify: Listener shows `imported 60 legacy animation clip(s)`; rollout populates with 60 entries (ATTACKFLINCHB_00 first, TURNR_00 last); `animationRange` covers `[0, 2488]`.
5. **Click `<<` repeatedly to walk through clips.** Verify: each combo selection scrubs the time-slider to that clip's stored range; spinner Start = clip's start frame; spinner End = clip's end frame.
6. **Modern-precedence policy:** with `EI_SNOWTROOPER.max` open, use MAXScript `setUserProp rootNode "Alamo_Anim_Clips" "MYCLIP"`, then File → Save → File → New → File → Open the same file. Verify: Listener does NOT show "imported N clips" (modern wins; legacy import skipped); rollout shows `MYCLIP` only. Confirms one-way upgrade legacy → modern; saving with modern props pins the file as modern.
7. **Export `EI_SNOWTROOPER.max`** via File → Export → Alamo Object (.alo). Verify: 60 sibling `.ala` files emit (`EI_SNOWTROOPER_ATTACKFLINCHB_00.ala`, ..., `EI_SNOWTROOPER_TURNR_00.ala`); `.export.log` shows `Animation: 60 clip(s) declared, 60 written`. Per Phase 11b.1's contract, each `.ala` covers its `[start, end]` frame range from the imported user props.
8. **Cross-tool round-trip:** open one of the emitted `.ala` files in Mike Lankamp's `alamo2max.ms` MAXScript importer (any supported Max version). Verify: no MAXScript Listener errors; scene reconstructs with the expected `nFrames` for that clip.

**Negative tripwire H** (manual sanity check during development): in `alamo_format/src/legacy_clip_scan.cpp`, flip one byte of `kLegacyUtilityClassIdBytes` (e.g. `0x90` → `0x91`). Rebuild. Re-open `CIS_SBD.max`. Expected: Listener silent (no "imported N clips" line); rollout combo shows `-- none --`. Confirms the importer is keyed off the exact recovered Class_ID and a regression mutating it would surface immediately.

### Tier 5 — Shadow-volume closed-volume validator (Phase 12)

Validates that meshes flagged as shadow-volume (via shader `MeshShadowVolume.fx` / `RSkinShadowVolume.fx`, or the legacy `_ALAMO_SHADOW_VOLUME = 1` int user-prop) form a closed 2-manifold. Open meshes get a one-line warning per offending mesh in `.export.log` and the MAXScript Listener; export is **not** aborted. Matches the legacy PG exporter's warn-not-abort behaviour. Catch2 covers the pure-C++ topology check (13 cases under `alamo_format/tests/shadow_volume_check_test.cpp`); the steps below cover the GUI export path that the batch harness doesn't exercise.

1. **Open Max** (any version with the current `.dle` installed) → Customize → Open MAXScript Listener.
2. **In the Listener, paste and Shift+Enter (single line):**
   ```
   resetMaxFile #noPrompt; b = Box name:"GUIClosedBox" width:10 length:10 height:10 pos:[0,0,0]; setUserProp b "Alamo_Shader_Name" "MeshShadowVolume.fx"
   ```
3. **File → Export → Alamo Object (.alo)**, save to a fresh path (e.g. `build/maxbatch/gui_phase12_closed.alo`). Verify: dialog reports "Export complete"; Listener has NO `max2alamo: WARNING: shadow-volume` line; the file's sibling `.export.log` contains no `WARNING: shadow-volume` line. **(Closed mesh → no warning.)**
4. **In the Listener, paste:**
   ```
   resetMaxFile #noPrompt; b = Box name:"GUIOpenBox" width:10 length:10 height:10 pos:[0,0,0]; convertToPoly b; polyOp.deleteFaces b #(1); setUserProp b "Alamo_Shader_Name" "MeshShadowVolume.fx"
   ```
5. **File → Export → Alamo Object (.alo)** to `build/maxbatch/gui_phase12_open.alo`. Verify: dialog reports "Export complete" (export NOT aborted); Listener shows `max2alamo: WARNING: shadow-volume mesh 'GUIOpenBox' has <N> non-manifold edge(s); the engine's stencil shadow pass will render incorrectly.`; the `.export.log` contains the same line. **(Open mesh → warning, but export succeeds.)**
6. **Optional — user-prop trigger path:** repeat step 4 but without the shader override; instead append `; setUserProp b "_ALAMO_SHADOW_VOLUME" 1`. Export; verify the warning still fires (the legacy MaxScript hook is honoured as a secondary trigger).

**Negative tripwire J** (manual sanity check during development): in `alamo_format/src/shadow_volume_check.cpp`, replace the `kCount != 2` filter with `< 1`. Rebuild. Re-run step 5. Expected: the warning no longer fires on the open box (the validator now incorrectly considers a single-boundary edge as closed). Confirms the topology check is keyed off the exact edge-incidence rule.

**Corpus baseline** (informational, no manual action): `python scripts/check_shadow_volume_corpus.py` over EaW + FoC corpus reports ~75% pass rate (2,392 closed / 3,168 shadow submeshes across 4,132 vanilla `.alo` files). The ~25% "open" remainder is vanilla authoring noise the legacy PG exporter would have warned on too — meshes with 1–4 non-manifold edges over hundreds of triangles. The threshold is set to 75% so a regression that makes the validator over-strict (rate drops) would fire the check.

### Tier 5 — Billboard orientation convention (Phase 12.1)

Validates that meshes authored with `Alamo_Billboard_Mode` (or the legacy `_ALAMO_BILLBOARDS` hook) render with their visible face pointing toward the camera in-engine. The authoring rule is documented in `docs/format-notes.md` "Billboard pivot convention" — the quad's visible-face normal must point along bone-local -Y. Automated coverage: `tests/maxscript/test_phase12_1_billboards_legacy_hook.ms` exercises the back-compat code path; corpus check (`scripts/inspect_billboard_orientation.py`) confirms 14/14 vanilla PARALLEL + SUNLIGHT_GLOW meshes follow the convention exactly. The steps below cover the in-engine visual verification that no headless harness can do.

1. **Open Max** (any version with the current `.dle` installed). Open the Utility panel → Alamo Utility.
2. **In the MAXScript Listener:**
   ```
   resetMaxFile #noPrompt
   p = Plane name:"BillboardTest" length:10 width:10
   rotate p (eulerAngles 90 0 0)    -- normal goes from +Z to -Y (required pivot)
   ResetPivot p; ResetXForm p; collapseStack p
   setUserProp p "Alamo_Billboard_Mode" 1   -- BBT_PARALLEL
   ```
3. **File → Export → Alamo Object (.alo)** to a fresh path (e.g. `build/maxbatch/billboard_visual.alo`). Verify the dialog reports "Export complete".
4. **Open the .alo in AloViewer** (https://github.com/GlyphXTools/alo-viewer; load via File → Open or drag-drop). Orbit the camera. Verify the textured quad rotates to follow the camera, always presenting its visible face. If the quad shows its back-face (untextured or alpha-key visible) or vanishes when viewed from a particular angle, the pivot rotation in step 2 was wrong -- the modder needs to rotate the mesh so its visible normal points along the bone's local -Y.
5. **Legacy-hook back-compat check** (in MAXScript Listener):
   ```
   resetMaxFile #noPrompt
   p = Plane name:"LegacyBillboardTest" length:10 width:10
   rotate p (eulerAngles 90 0 0); ResetPivot p; ResetXForm p; collapseStack p
   setUserProp p "_ALAMO_BILLBOARDS" 1    -- legacy hook only
   ```
   Export, open in AloViewer. Verify identical billboard behavior to step 4 -- `resolve_billboard_mode` should have mapped the legacy hook to BBT_PARALLEL.

**Negative tripwire K** (manual sanity check during development): in `scene_walker.cpp::resolve_billboard_mode`, remove the `_ALAMO_BILLBOARDS` fallback branch. Rebuild. Re-run step 5. Expected: the AloViewer-loaded mesh no longer billboards (renders with its authored static orientation). Confirms the fallback is what wires the legacy hook through.

### Authoring convention — bind pose lives at frame 0

The walker calls `IGameScene::SetStaticFrame(0)` ([`scene_walker.cpp:1966`](../max2alamo/src/scene_walker.cpp:1966)) and samples *every* node's transform at TimeValue 0 for the bind pose written into the `.alo`. Animation tracks are sampled separately across the `Alamo_Anim_Start..Alamo_Anim_End` range. This means **the scene state at frame 0 IS the bind pose the engine renders before / between animations**, regardless of where the scene's `animationRange` actually starts.

Pitfalls observed in the wild:

- **`animationRange` starting after frame 0.** A scene with `animationRange = (interval 373f 2488f)` has *no keyframes at frame 0*; controllers (especially `Position_XYZ`) fall back to leftover initial values that may not match the rest pose. EI_SNOWTROOPER's gun mesh was authored with `animationRange = (373, 2488)` and a `Position_XYZ` controller on `Box01`; at frame 0 the gun lands at world `(-4.49, -1.06, 0)` (floor) while at every animated frame ≥ 373 it sits in the hand at `|Box01 - B_Gun| = 7.74`. The walker correctly encodes whatever's at frame 0; the engine then renders the bind pose on the floor. Fix at the authoring layer: re-key the position at frame 0 to match the intended bind, OR shift the entire animation range to start at 0.
- **`b.rotation =` keyframe orbits the position.** Setting a bone's rotation keyframe at frame 0 to a value that differs from the bone's static node-TM rotation will orbit the bone's translation around its local pivot. Combined with `BoneSys.createBone <from> <to>` baking a rotation into node TM derived from the from-to direction, this can silently corrupt the bind pose. The synthetic Phase 14b fixture hit this: `createBone [500,0,0] [500,1000,0]` (direction +Y) baked +90° Z into node TM; the harmless-looking `at time 0 (hand.rotation = (eulerAngles 0 0 0))` then orbited HandBone from `(500, 0, 0)` to `(0, -500, 0)`. Fix: either keep the createBone direction collinear with the bone's intended local +X (so node TM stays identity), or set the time-0 rotation keyframe to *match* the createBone-baked rotation.

If you suspect a bind-pose mismatch, dump the encoded state with [`tests/maxscript/verify/_chain_dump.py`](../tests/maxscript/verify/_chain_dump.py) and compare to a Max-side `at time 0 (...)` dump of `node.transform.row4` for the same nodes. Mismatches between the two are *always* in the source scene, not in the walker (verified Phase 14d, [#87](https://github.com/DrKnickers/max2alamo-2026/issues/87)).

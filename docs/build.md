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

Every plugin / walker / format-library change must pass the full export test pyramid before merge. Tiers 1–3 are automated by `scripts/run-max-tests.ps1`; Tier 4 is a manual checklist.

### Tier 1 — Tier-1 invariants (automated, two modes)

`tests/maxscript/verify/validate_alo.py` runs on every exported `.alo`. Two modes calibrated against different ground truths:

- **Strict mode (default for harness exports)** — adds checks our walker output is supposed to satisfy: sub-1e-3 normal/tangent length, tangent perpendicularity `|dot(T,N)|<=0.15`, per-vertex weights sum to 1.0, non-negative weights. Vanilla content routinely violates these; our pipeline never does, so a violation in our output is a real regression.
- **Loose mode** (`--loose`) — structural baseline only: bone parent topology, index/vertex consistency, name/billboard ranges, connection count = `(meshes + lights)`. Calibrated against the entire vanilla EaW+FoC corpus (2066/2066 files pass).

Run `python scripts/sweep_corpus_validator.py` to confirm the loose-mode validator stays vanilla-compatible after walker / format changes. Every file in `tests/corpus/` should pass; if a vanilla file fails, the validator is too strict.

### Tier 2 — Writer round-trip (automated)

Each exported `.alo` is fed through `build\tools\alo_roundtrip\Release\alo_roundtrip.exe`. Non-zero exit means the writer's re-serialisation diverges from the original bytes — i.e. a regression in the format-library writer. Catches drift for free without per-test boilerplate.

### Tier 3 — Feature-specific verifiers (automated)

Each `tests/maxscript/test_*.ms` ships with a paired `verify_test_*.py` that asserts the behaviour the test was written to pin. When you add a feature, add its Tier 3 verifier.

```powershell
# Tiers 1-3 in one shot:
powershell -File scripts/run-max-tests.ps1
```

### Tier 4 — Manual smoke tests (release / major-PR sign-off)

Things batch mode can't verify. Run these for releases or whenever a PR's behaviour can't be inferred from on-disk structure alone:

- [ ] **Mike Lankamp's `alamo2max` MAXScript importer.** Open the exported `.alo` in his importer (any supported Max version). No errors in the MAXScript listener; scene reconstructs with the expected mesh / bone / material set.
- [ ] **AloViewer.** Drag the exported `.alo` onto AloViewer. Renders without crash; geometry has expected shape and orientation; materials visible (textured surfaces show texture, not solid colour).
- [ ] **In-game (EaW / FoC).** Drop the `.alo` into a mod folder and load the relevant unit. Loads without crash; renders correctly; for animation work, animations play; hardpoints fire from expected positions.

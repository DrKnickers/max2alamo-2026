# Development log

Single-file project status + history. Open this first when picking up a new session — it links out to the deeper documents (`docs/format-notes.md`, `docs/build.md`, `docs/corpus.md`) and to specific commits.

---

## Quick orientation

| Question | Answer |
|---|---|
| What is this? | Modern 3ds Max 2026 plugin for the **Alamo** (`.alo` / `.ala`) format used by *Star Wars: Empire at War* and *Forces of Corruption*. Clean-room rewrite of Petroglyph's Max 9 closed-source `max2alamo.dle`. |
| Who is building it? | Claude (Anthropic), authoring under the direction of [@DrKnickers](https://github.com/DrKnickers). See [Authorship](../README.md#authorship). |
| What language / build? | C++17, CMake (with VS 2022 / 2026 generators), MSVC. |
| Where does the format library code live? | `alamo_format/` (no Max SDK deps; built in CI). |
| Where does the Max plugin code live? | `max2alamo/` (built locally only — Max SDK is not redistributable). |
| Where does the test corpus live? | `tests/corpus/` (gitignored, Lucasfilm IP). See [`docs/corpus.md`](corpus.md) to build one. |
| Where do reverse-engineering artifacts live? | `re/` (gitignored). |

---

## Phase status

| Phase | Theme | Status | Acceptance proof |
|---|---|---|---|
| 0 | Repo + spec mastery | ✅ shipped | Repo green, [`docs/format-notes.md`](format-notes.md) populated |
| 0.5 | Reverse-engineering recon | ✅ shipped | Format spec resolved beyond the Petrolution docs |
| 1 | Format library reader + `alo_dump` | ✅ shipped | 2066 / 2066 vanilla `.alo` files parse cleanly |
| 2 | Format library writer + `alo_roundtrip` | ✅ shipped | 4929 / 4929 vanilla `.alo` + `.ala` files round-trip byte-identical |
| 3 | Max 2026 plugin scaffold | ✅ shipped | Plugin loads in Max; "Alamo Object" appears in Export menu |
| 4 | Static geometry export | ✅ shipped | Textured cube exports from Max, imports cleanly into Mike's importer, and renders in AloViewer |
| 4c-fix | Bone WorldTM bake | ✅ shipped | [PR #17](https://github.com/DrKnickers/max2alamo-2026/pull/17) — multi-mesh scenes export at their authored positions (no longer collapsed to origin) |
| 5a | Real bone hierarchy + local matrices | ✅ shipped | [PR #24](https://github.com/DrKnickers/max2alamo-2026/pull/24) — `IGAME_BONE` walk with `GetLocalTM`; verified by `test_bone_hierarchy.ms` |
| 5b | Single-bone skinning via IGameSkin | ✅ shipped | [PR #25](https://github.com/DrKnickers/max2alamo-2026/pull/25) — skinned cylinder exports with per-vertex dominant-bone refs; connects to Root |
| 5c | Multi-bone weighted skinning | ✅ shipped | smooth-painted joint splits 50/50 between flanking bones with weights summing to 1.0; verified by `test_smooth_skinned_joint.ms` |
| 5d | `Alamo_*` user-property family — simple flag reads | ✅ shipped | Walker reads `Alamo_Export_Geometry` (opt-out), `Alamo_Geometry_Hidden`, `Alamo_Collision_Enabled`, `Alamo_Billboard_Mode`; verified by `test_alamo_user_props.ms` |
| 5e | `Alamo_*` user-property family — helpers-as-bones | ✅ shipped | `IGAME_HELPER` nodes with `Alamo_Export_Transform=true` join the skeleton as bones; verified by `test_helper_as_bone.ms` |
| 5f | `Alamo_*` user-property family — remaining props research | ✅ shipped (research-only) | Format research resolved: 3 of 4 props have **zero binary representation** in `.alo`; the 4th (`Alt_Decrease_Stay_Hidden`) is a proxy-chunk field deferred to Phase 7. No walker work needed today. |
| 7a | Format library: `ExportLight` / `ExportProxy` structs + builders | ✅ shipped | `0x1300`/`0x1301`/`0x1302` light chunks + `0x603` proxy chunks emit through `build_alo`; connection-counts in `0x601` reflect `(meshes + lights)` and proxy counts; 9 new unit tests pass; mixed scene round-trips byte-identical. |
| 7b.1 | Walker: `IGAME_LIGHT` Omni + Directional + harness tests | ✅ shipped | `walk_lights` via `igame->GetIGameNodeByType(IGAME_LIGHT)` emits per-light synth bones + ExportLight; 6 new harness tests (basic, primaries, atten on/off, directional, mesh+light mix, hidden); `.export.log` Lights summary; Tier 1 validator extended; corpus still 2066/2066. |
| 7b.2 | Walker: Spotlight + `.Target` sibling bone | ✅ shipped | TSpot / FSpot emit as Spotlight (type=2); TargetSpot also emits a sibling `<name>.Target` bone at `INode::GetTarget()`'s world TM (matches vanilla EB_ICC_LANDINGPAD pattern). Empirically confirmed: IGameProperty returns Max-UI **degrees** for cone angles, walker converts to radians for disk format. |
| 7c.1 | `Alamo_Proxy` helper plugin class registered | ✅ shipped | Faithful clone of the legacy PG Max-9 plugin's helper: appears under Create > Helpers > Standard > Alamo Proxy. Stable `Class_ID(0x6ed3a4f1, 0x8a721d04)`. `NonLocalizedClassName` / `InternalName` "Alamo_Proxy" matches Mike Lankamp's `alamo2max.ms:1344` so his importer reaches us. Walker side is 7c.2. |
| 7c.2 | Walker: `Alamo_Proxy` instances → ExportProxy + harness tests | ✅ shipped | `walk_proxies` detects helpers by `Class_ID == kAlamoProxyClassID` (not name prefix); emits per-proxy synth bone + ExportProxy; reads `Alamo_Geometry_Hidden` (fallback to `IsNodeHidden()`) and `Alamo_Alt_Decrease_Stay_Hidden`; mutex with Phase 5e helpers-as-bones; 5 new harness tests; `.export.log` Proxies summary. |
| 7d | Acceptance: full integration test (skinned mesh + bones + lights + proxies) | ✅ shipped | `test_phase7_acceptance` combines 2 real bones + helper-as-bone + skinned cylinder + static box + Omni + TargetSpot + 2 proxies in one scene (10 bones / 2 meshes / 2 lights / 2 proxies / 4 connections). Tier 1 strict clean, Tier 2 byte-identical, Tier 3 verifier with **31 interaction-shaped assertions** across 8 groups (skeleton/mutex/wiring/lights/proxies/connections/matrices/log) all pass; 3× byte-identical determinism (SHA `56FCEAF0…`); 24/24 harness, 60/60 unit, 2066/2066 corpus; tripwires confirm assertions actually bite. |
| 8a | Format library: typed `.ala` read/write pipeline (`AlaAnimation` struct + `build_ala` + `read_ala` + `ala_typed_roundtrip` CLI + 21 Catch2 tests) | ✅ shipped | **2863/2863 vanilla `.ala` byte-identical** via typed pipeline (1500/1500 FoC + 1363/1363 EaW — exceeded plan's parse-clean bar for EaW). Design key: typed fields + raw payload preservation per leaf, so read-then-write is identity by construction. 3× full-corpus determinism; `alo_dump` output identical across runs; 81/81 unit tests; non-regression on `alo_roundtrip` (4929/4929) and `.alo` corpus sweep (2066/2066); three negative tripwires fire on the expected assertion. |
| 8b | Walker rotation pass: `walk_animation()` samples `IGameNode::GetLocalTM(t)` per frame for every bone in `bone_map`; packs int16 XYZW with sign canonicalisation; exporter writes `.ala` next to `.alo` when at least one bone has a rotation track | ✅ shipped | `test_rotation_keyframes` (single bone keyframed 0°→90° about Z over 31 frames) round-trips through Tier 1 (`.alo` strict), Tier 2 (chunk-tree), Tier 2-ala (typed pipeline), Tier 3 (17-assertion verifier). Frame[0]=(0,0,0,1), frame[30]=(0,0,0.7071,0.7071) within 1e-3. 25/25 harness (existing 24 still produce only `.alo`; no spurious `.ala`); 3× byte-identical determinism (`.alo` `3791F274…`, `.ala` `2B60515B…`); existing test_phase7_acceptance still SHA `56FCEAF0…` post-rebuild. Non-regression: 4929/4929 chunk-tree · 2066/2066 .alo sweep · 2863/2863 .ala typed-pipeline · 81/81 unit. Three negative tripwires fire on the expected assertion. |
| Utility UI | Faithful clone of the legacy PG Alamo Utility panel | ✅ shipped | Three rollouts (Node Export Options / Quick Selection / Animation Settings) appear under Utilities > More... > Alamo Utility; checkboxes/radios round-trip Alamo_* user properties on the selected node |
| 6a | Effects11 shader stubs for Max 2026 | ✅ shipped | [PR #18](https://github.com/DrKnickers/max2alamo-2026/pull/18) — all 39 PG shaders load in Max 2026's DXSM with PG parameter UIs |
| 6b | Per-vertex tangent + binormal export | ✅ shipped | [PR #19](https://github.com/DrKnickers/max2alamo-2026/pull/19) — MikkT via `IGameMesh::GetFaceVertexTangentBinormal`; bump shading works |
| 6c | Per-material parameter export | ✅ shipped | [PR #20](https://github.com/DrKnickers/max2alamo-2026/pull/20) + [#22](https://github.com/DrKnickers/max2alamo-2026/pull/22) — DXMaterial ParamBlock → typed `0x10103/6` chunks; float3 alpha=0 convention |
| 6d | Coord-frame banner in `.export.log` | ✅ shipped | [PR #21](https://github.com/DrKnickers/max2alamo-2026/pull/21) — every export documents `Z up, -Y forward, +X right` |
| Test harness | Max-side regression suite | ✅ shipped | [PR #23](https://github.com/DrKnickers/max2alamo-2026/pull/23) + [#26](https://github.com/DrKnickers/max2alamo-2026/pull/26) — `scripts/run-max-tests.ps1` runs 6 end-to-end tests via `3dsmaxbatch` |
| 7 | Lights, hardpoints, proxies | ✅ shipped | Format library + walker + helper plugin class all in place; 24/24 harness covers Omni/Directional/Spotlight/`.Target`/proxies/flags + the 7d full-scene integration. In-game fighter Tier 4 deferred to Phase 8/9. |
| 8 | Animation export incl. visibility tracks | in progress | Walk cycle + animated visibility play correctly in-game. **Sub-phases:** 8a typed `.ala` read/write pipeline ✅; 8b walker rotation pass ✅; 8c walker translation+scale + FoC pool packing, 8d visibility tracks, 8e full integration acceptance pending. |
| 9 | Polish + v1 release | pending | v1.0 tag with `.dle` published via GitHub Releases |

Each main phase has a corresponding GitHub issue (`#1` for Phase 0.5 through `#10` for Phase 9). Sub-phases (4c-fix, 5a/5b, 6a-d, test harness) shipped as standalone PRs without dedicated issues.

---

## Workflow conventions

| Convention | Detail |
|---|---|
| Branch model | `main` is always buildable. Phase work happens on `phase/N-short-name`; doc work on `docs/<topic>`. |
| PR-per-change | Every commit reaches `main` through a PR. Branch protection enforces this (CI must pass; admins can self-merge). |
| Merge style | `--rebase` to preserve the per-commit messages (kept descriptive). `--delete-branch` after merge. |
| CI builds | Format library + tools + tests, on every push and PR. **Does not build the Max plugin** (SDK non-redistributable). |
| Plugin builds | Locally only; `cmake --build build --config Release --target max2alamo`. |
| Corpus | Not committed (Lucasfilm IP). Each contributor builds their own per [`docs/corpus.md`](corpus.md). |
| Commit messages | Conventional prefixes (`feat`, `fix`, `docs`, `chore`, `ci`). Body explains *why* + key decisions. |
| Reverse-engineering | Findings paraphrased in [`docs/format-notes.md`](format-notes.md). Decompilations / Ghidra projects stay in gitignored `re/`. Never commit Petroglyph-derived code. |

---

## Repo lockdown (since the public flip mid-Phase 2)

The repo is public so GitHub Actions runs without quota concerns, but it's not open for contribution yet. Three layers protect it:

- **Branch protection on `main`**: PR required, status checks required, linear history, no force pushes, no deletions.
- **Interaction limits**: `collaborators_only`, refreshed monthly by `.github/workflows/renew-interaction-limits.yml` (after one-time PAT setup; see workflow header) or manually via `scripts/renew-interaction-limits.sh`.
- **Actions fork-PR contributor approval**: `all_external_contributors`. Default GITHUB_TOKEN permissions: `read` only.

Wiki / Discussions / Projects are disabled.

---

## Where things live

| Thing | Path |
|---|---|
| Format library headers | `alamo_format/include/alamo_format/` (incl. `shader_table.h` for per-shader param specs) |
| Format library sources | `alamo_format/src/` |
| Format library tests | `alamo_format/tests/` (Catch2 v3 via FetchContent) |
| Standalone CLIs | `tools/alo_dump/`, `tools/alo_roundtrip/`, `tools/alo_synth/` (synth `.alo` from a hard-coded `ExportScene`) |
| Max plugin sources | `max2alamo/src/` (incl. `alamo_utility.cpp` for the command-panel Utility) |
| Max plugin resources | `max2alamo/resources/` (.def, .rc, `utility_dialogs.rc` + `utility_resource.h` for the Utility's 3 rollout templates) |
| Max-side test harness | `tests/maxscript/test_*.ms` scenes + `tests/maxscript/verify/verify_test_*.py` assertions + `tests/maxscript/verify/_alo.py` parser library |
| Max-side test runner | `scripts/run-max-tests.ps1` (dispatches `3dsmaxbatch.exe` per test, runs paired Python verifier) |
| Effects11 shader stubs | `shaders/max-preview/*.fx` (39 files, regenerated from a manifest by `scripts/generate-max-preview-stubs.py`) |
| Corpus-sweep validator runner | `scripts/sweep_corpus_validator.py` (loose-mode Tier 1 against every file in `tests/corpus/`; calibration check that the validator hasn't drifted from PG-shipped content) |
| Tangent diagnostic tools | `scripts/compare-tangents.py` / `scripts/dump-tangents.py` (one-shot research scripts used to settle the MikkT vs vanilla decision) |
| Format spec (working ref) | `docs/format-notes.md` |
| Build instructions | `docs/build.md` |
| Corpus extraction guide | `docs/corpus.md` |
| MEG extractor (script) | `scripts/extract-meg.ps1` |
| Interaction-limit refresh | `scripts/renew-interaction-limits.sh`, `.github/workflows/renew-interaction-limits.yml` |
| **Test corpus (local-only)** | `tests/corpus/eaw/`, `tests/corpus/foc/`, `tests/corpus/ala-eaw/`, `tests/corpus/ala-foc/` |
| **Ghidra projects (local-only)** | `re/ghidra_project/`, `re/output/`, `re/scripts/` |
| Local build outputs | `build/` (gitignored) |
| Vanilla MEG sources | `D:\SteamLibrary\steamapps\common\Star Wars Empire at War\GameData\Data\models.meg` (EaW), `\corruption\Data\models.meg` (FoC) |
| Max 2026 install | `C:\Program Files\Autodesk\3ds Max 2026\` |
| Max 2026 SDK | `C:\Program Files\Autodesk\3ds Max 2026 SDK\maxsdk\` |
| Ghidra | `C:\Tools\ghidra_12.0.4_PUBLIC\` (with JDK 21 at the standard Microsoft OpenJDK path) |

---

## Per-phase detail

### Phase 0 — Repo + spec mastery (shipped)

Set up the repo skeleton, CI workflow, and the working format reference. Commit: [1ce186c](https://github.com/DrKnickers/max2alamo-2026/commit/1ce186c) (initial), [7c43cd0](https://github.com/DrKnickers/max2alamo-2026/commit/7c43cd0) (CI fix).

- Created `alamo_format` static lib stub, `alo_dump` / `alo_roundtrip` CLI stubs, top-level CMake.
- GitHub Actions workflow (`.github/workflows/ci.yml`) building format library + tools on every push.
- `docs/format-notes.md` written from the Petrolution AloFileFormat / AlaFileFormat docs, cross-referenced with line numbers in Gaukler's Blender plugin.
- `.gitignore` excluding `re/`, `tests/corpus/`, build outputs.

### Phase 0.5 — Reverse-engineering recon (shipped)

Set out to use Ghidra on Petroglyph's `max2alamo.dle` and Mike Lankamp's `alamo2max.dlu` to recover the missing format details. Outcome was **better than planned**: Mike's `alamo2max.ms` MAXScript file (which we'd assumed was just UI scaffolding) turned out to be a near-complete `.alo` and `.ala` reader in plain MAXScript. Combined with a strings dump of the original `.dle`, we resolved the major format-knowledge gaps without needing decompilation. Commit: [fd23caa](https://github.com/DrKnickers/max2alamo-2026/commit/fd23caa).

Resolved:
- Multi-bone vertex packing (B4I4 layout: 4 × `uint32 boneIdx` + 4 × `float weight`, 16 + 16 bytes at vertex tail).
- `0x10005` (rev 1, 128 B/vertex) vs `0x10007` (rev 2, 144 B/vertex with 16-byte unused tail) distinction.
- Single-bone `RSkin*` family (vs B4I4) — used by skin-shader-prefixed shaders.
- Hardpoints are not a separate chunk type — they're proxies (`0x603`) named `HP_*` by convention.
- Light field layout (type, RGB, intensity, atten parameters).
- Helper-object conventions: `Alamo_Export_Geometry`, `Alamo_Export_Transform`, `Alamo_Geometry_Hidden`, `Alamo_Collision_Enabled`, `Alamo_Billboard_Mode`, `Alamo_Alt_Decrease_Stay_Hidden` (user properties), plus `_ALAMO_BONES_PER_VERTEX` etc. (MAXScript hooks).
- Quaternion compression: `int16 = round(q * 32767)`, XYZW order.
- Visibility track encoding: 1 bit per frame, LSB-first within each byte; chunk only emitted if any frame is hidden.
- EaW vs FoC `.ala` storage difference (per-bone inline vs file-scope pool with indices).

Ghidra was loaded for both binaries (artifacts gitignored at `re/ghidra_project/`); decompilation was not needed.

### Phase 1 — Format library reader + `alo_dump` (shipped)

Implemented `chunk_io.h`, `chunk_reader.cpp`, `chunk_writer.cpp` (writer surface needed for synthetic test data), and the `alo_dump` CLI. Commits: [f38a37c](https://github.com/DrKnickers/max2alamo-2026/commit/f38a37c) (implementation), [151828e](https://github.com/DrKnickers/max2alamo-2026/commit/151828e) (self-review fixes), [0cce6dd](https://github.com/DrKnickers/max2alamo-2026/commit/0cce6dd) (corpus tooling + docs).

Real-data validation produced two corrections vs. the earlier guesses:
- High bit `0x80000000` of the chunk size word marks a **container**, not a leaf (semantics had been backwards).
- `0x201` skeleton info chunk is always 128 bytes: `uint32 boneCount` + 124 reserved zero-padded bytes.
- `0x402` mesh info also always 128 bytes (40 documented + 88 reserved).

`alo_dump` parses **2066 / 2066** vanilla `.alo` files cleanly. 18 / 18 unit tests pass. Independent PowerShell framing validator agrees with the C++ reader file-for-file.

### Phase 2 — Format library writer + `alo_roundtrip` (shipped)

Added `chunk_tree.h` (`ChunkNode` struct) plus the `read_chunk_tree` / `write_chunk_tree` API. Implemented in `alo_reader.cpp` / `alo_writer.cpp`. The `alo_roundtrip` CLI reads, re-serializes, and byte-diffs. Commit: [19197c7](https://github.com/DrKnickers/max2alamo-2026/commit/19197c7).

**Approach**: structural pass-through. Chunk tree preserves payload bytes verbatim per leaf; writer re-emits via `ChunkWriter`. No semantic interpretation — mini-chunks, vertex data, reserved padding, unknown FoC track pools all round-trip because we never look inside.

**Result**: **4929 / 4929 byte-identical round-trips (100.00%)** across 2066 `.alo` files and 2863 `.ala` files (the latter being a free cross-format check since both formats share the chunk framing). Acceptance bar was ≥ 95 %.

The semantic decoding into `AloModel` / `Bone` / `Mesh` / `Material` structs is intentionally a **separate layer** added in Phase 4+ when the Max plugin needs to *construct* content from a scene. Round-trip correctness does not require semantic understanding.

### Phase 3 — Max 2026 plugin scaffold (shipped)

The actual `.dle`. Commit: [d119b52](https://github.com/DrKnickers/max2alamo-2026/commit/d119b52) (merged via [PR #11](https://github.com/DrKnickers/max2alamo-2026/pull/11)).

- `max2alamo/CMakeLists.txt` — auto-detects the SDK at standard install paths; cleanly skips itself (with a CMake STATUS message) when no SDK is present, so CI keeps building the rest of the project. Defines `NOMINMAX` + `WIN32_LEAN_AND_MEAN` + Unicode + `_CRT_NON_CONFORMING_SWPRINTFS`.
- `plugin_entry.cpp` — five required exports (`LibDescription`, `LibNumberClasses`, `LibClassDesc`, `LibVersion`, `CanAutoDefer`) plus `DllMain` capturing `HINSTANCE`.
- `alo_export.{h,cpp}` — `SceneExport` subclass; registers `.ALO` extension and "Alamo Object" descriptions; stub `DoExport()` pops a Phase 3 dialog and returns `IMPEXP_FAIL`.
- `max2alamo.def` — five symbols by ordinal.
- `max2alamo.rc` — VERSIONINFO so Explorer shows real metadata.
- **Stable Class_ID**: `(0x6ed3a4f1, 0x2b9c7d05)`. **Must not change** once shipped — Max persists it in `.max` scenes that reference the plugin; changing it would orphan exports.

Verified in-Max by user: plugin loads, "Alamo Object (*.ALO)" appears in File → Export, Phase 3 dialog displays correctly when invoked.

### Phase 4 — Static geometry export (shipped)

The first phase where the plugin actually writes real `.alo` files from a Max scene. Shipped in three sub-PRs that each had their own scope:

- **4a** ([PR #14](https://github.com/DrKnickers/max2alamo-2026/pull/14)) — `ExportScene` POD struct family (`ExportVertex` / `ExportMaterial` / `ExportSubmesh` / `ExportMesh` / `ExportBone` / `ExportScene`) in the format library, plus the IGame-based `scene_walker` in `max2alamo/` that populates it. The airlock between Max and the format library.
- **4b** ([PR #15](https://github.com/DrKnickers/max2alamo-2026/pull/15)) — `build_alo(ExportScene) -> std::vector<ChunkNode>` serializer. Host-agnostic, unit-tested.
- **4c** ([PR #16](https://github.com/DrKnickers/max2alamo-2026/pull/16)) — `DoExport` wired through the pipeline; material extraction (Standard + DirectX Shader); `Alamo_Shader_Name` node user-property override; `.export.log` diagnostic written next to every export. Plus three corrective fixes discovered during real-world testing — see below.

Format-spec discoveries made during the work (all flow to [`format-notes.md`](format-notes.md)):

- **All vertex chunks on disk use 128 or 144 bytes per vertex regardless of the format-name string in `0x10002`.** The format name (`alD3dVertNU2`, `alD3dVertN`, etc.) selects which fields the engine reads; the on-disk size is always the full B4I4 layout. Confirmed across 9000+ vanilla submeshes (corpus survey). Our writer always emits `0x10007` (144 B/vertex) with neutral defaults in unused slots.
- **`0x10000` (geometry) is a SIBLING of `0x10100` (material) inside `0x400`, not nested.** Vanilla layout alternates `material[0]` / `geometry[0]` / `material[1]` / `geometry[1]` / … at the parent level. AloViewer rejected the file until this was fixed.
- **`0x10001` is a fixed-size 128-byte chunk** (8 bytes used: vertexCount + faceCount; 120 bytes reserved zeros). Same pattern as `0x201` / `0x402`.
- **Bone matrix is 4 rows × 3 columns stored column-major** (NOT row-major). Identity = `1,0,0,0, 0,1,0,0, 0,0,1,0`. The row-major mistake produced a degenerate transform that collapsed all geometry onto the (1,1,1) diagonal — exactly the "wedge" shape that appeared in Max 9 imports. This was the final structural bug, and fixing it unblocked AloViewer rendering, Mike's importer geometry, and (presumably) EaW in-game rendering.
- **Vanilla static-prop convention**: skeletons always have at least Root + one named non-Root bone per mesh. Mike's importer deletes Root after reading; vertex / connection bone references must land on real bones or his skin setup crashes. Our walker now emits a per-mesh attachment bone for each exported mesh.

A diagnostic that paid for itself many times over: every export drops a `<file>.export.log` next to the `.alo` listing every material's class, all texture maps (slot ID + filename), and the final shader / texture chosen. Surfaced both real bugs (texture-from-wrong-slot turning out to be a Max workflow issue, not ours) and confirmed correct behaviour when there *was* none.

Workflow learning: Max 2026 cannot compile EaW's DX9-era `.fx` shaders via its DirectX Shader material — they're 19-year-old HLSL that the modern compiler rejects. Added the `Alamo_Shader_Name` node user property as an explicit shader-name override, fitting the existing legacy `Alamo_*` user-property convention. Standard material on the mesh + this property gives full control without needing Max's effect compilation to work at all.

Acceptance verified by user: cube imports correctly in Mike's importer (no crash, real 3D geometry), renders correctly in AloViewer. EaW in-game test deferred to user discretion.

### Phase 4c-fix — Bone WorldTM bake ([PR #17](https://github.com/DrKnickers/max2alamo-2026/pull/17), shipped)

First real-content user test surfaced a bug Phase 4c had punted: the per-mesh attachment bone matrix was hardcoded to identity (with a comment that the WorldTM bake was "Phase 5 work"). Combined with vertex positions in object space, every mesh in a multi-mesh scene rendered stacked at the world origin — Box at (10,0,5) and Sphere at (-15,20,5) in Max both came out at (0,0,0) in AloViewer.

Fix: bake `IGameNode::GetWorldTM().ExtractMatrix3()` into the bone's 12-float column-major matrix at bone-allocation time. The encoding helper got extracted as `encode_matrix3` so Phase 5a's local-TM path could reuse it. Default-pivot meshes only; full `ObjectTM` handling for pivot-offset meshes deferred to Phase 5.

### Phase 6a — Effects11 shader stubs ([PR #18](https://github.com/DrKnickers/max2alamo-2026/pull/18), shipped)

Max 2026's DirectX Shader material runtime rejects every PG stock `.fx` file because they use the DX9 `fx_2_0` effect framework (inline render states, `VertexShader = (compiled_var)` assignment style). Max 2026 wants Effects11 (`technique11`, state objects, `SetVertexShader(CompileShader(vs_5_0, ...))`). This is **framework-level, not patchable** — `fxc` accepts the patched DX9 effects but Max still rejects them at runtime.

Without stubs, the canonical Petroglyph authoring workflow ("Material Editor → DirectX Shader → load `MeshBumpColorize.fx` → param rollout appears → wire textures → preview → export") breaks at "load." This PR adds same-named Effects11 stubs that:
- have the same parameter names, types, and `UIName` annotations as the PG shaders
- render simplified Blinn/Lambert in the Max viewport (visual fidelity is *not* the goal — UI fidelity is)
- exist purely Max-side; the exported `.alo` references only the shader filename, and EaW resolves it against its own `Data/Art/Shaders/` at runtime

All 39 PG shaders from `corruption/Mods/Empire-at-War-Source-Files/src/FOC/Data/Art/Shaders/` are covered. The set is regenerated from a manifest in [`scripts/generate-max-preview-stubs.py`](../scripts/generate-max-preview-stubs.py); new shaders are added by appending one manifest entry. Source of truth for params: Gaukler's `material_parameter_dict` merged with PG's `.fxh` headers. See [`shaders/max-preview/README.md`](../shaders/max-preview/README.md).

`alDefault.fx` was the validation target — full round-trip (load in Max → apply to cube → export → AloViewer rendering correctly with `Material: alDefault.fx`) confirmed the Effects11 dialect was right before the other 38 were generated.

### Phase 6b — Per-vertex tangent + binormal export ([PR #19](https://github.com/DrKnickers/max2alamo-2026/pull/19), shipped)

PG's bump-mapped shaders (`MeshBumpColorize`, `RSkinBumpColorize`, `MeshBumpReflectColorize`, `RSkinBumpReflectColorize`, `Tree`, `TerrainMeshBump`, `Planet`) do per-pixel lighting in tangent space. The VS reads `In.Tangent` / `In.Binormal` from the vertex record, builds a world-to-tangent matrix, and transforms the light + half-angle vectors into that frame for the PS. Phase 4 left those vertex fields at zero — producing a degenerate tangent matrix and a distinctive half-textured / half-white rendering artifact that surfaced during Phase 6a validation.

This populates them per face-corner via `IGameMesh::GetFaceVertexTangentBinormal(face, corner)` — the documented shared index into IGame's tangent + binormal arrays. The first iteration reused `face->texCoord[corner]` which produced wrong-face tangents on UV-shared geometry (cubes); the dump diagnostic surfaced this immediately (`|dot(T, N)| = 0.39` — not perpendicular).

**Why MikkT specifically:** empirical comparison across ~22k vertices in 11 vanilla `.alo` files showed vanilla PG content is **not** internally consistent — different submeshes were authored against different tangent algorithms (median per-vertex angle vs Lengyel/MikkT-style ranges from 0.5° to >90° depending on the source mesh). There is no single "vanilla algorithm" to match. MikkTSpace (Max 2026 default; what Substance Painter, Blender's normal-map bake, Marmoset, and Max's own bake since 2014 use) is the modern standard. The diagnostic scripts [`scripts/compare-tangents.py`](../scripts/compare-tangents.py) and [`scripts/dump-tangents.py`](../scripts/dump-tangents.py) settled this empirically.

### Phase 6c — Per-material parameter export ([PR #20](https://github.com/DrKnickers/max2alamo-2026/pull/20) + [PR #22](https://github.com/DrKnickers/max2alamo-2026/pull/22), shipped)

Vanilla material chunks (`0x10100`) carry typed per-material parameter values as `0x10106` FLOAT4 + `0x10103` FLOAT mini-chunks, but Phase 4c only wrote the shader name + (optional) `BaseTexture`. The engine fell back to compile-time shader defaults (`Specular = (1,1,1)`, `Shininess = 32`) — combined with PG's `* 2.0` brightness multiplier in `MeshBumpColorize`, this produced saturated highlights even when the modder had dialled Specular down in Max.

Pipeline:

```
Max DirectX Shader material ParamBlock(0)
   -- IDxMaterial docs: hosts the effect params with names matching the .fx file
↓
scene_walker::extract_pblock_params
   -- iterates the block; maps TYPE_FLOAT / TYPE_RGBA / TYPE_FRGBA /
   -- TYPE_POINT3 / TYPE_POINT4 / TYPE_BITMAP -> typed MaterialParam
↓
ExportMaterial::params  (new field)
↓
shader_table::params_for / contains  (new format-library module)
   -- maps shader filename -> canonical ordered (name, kind, default) list.
   -- Walker values override defaults; defaults emit when source didn't override.
   -- Source of truth: Gaukler's material_parameter_dict + empirical vanilla.
↓
alo_build::build_submesh_material  (updated)
   -- emits in canonical vanilla order. Scalars/vectors always emit; textures
   -- only when filename non-empty. Falls back to Phase 4 layout for unknown shaders.
↓
0x10106 / 0x10103 chunks  (new build_float_param, build_float4_param writers)
```

[PR #22](https://github.com/DrKnickers/max2alamo-2026/pull/22) added the float3-declared-param alpha=0 convention: vanilla content writes 0.0 in the 4th slot of params declared `float3` in PG's `.fxh` (Emissive, Diffuse, Specular, CityColor…) while preserving the 4th slot for genuine `float4` params (Colorization, UVOffset, Color, DebugColor, Atmosphere). Max's `TYPE_FRGBA` returns AColor with alpha=1; the writer zeros the alpha when `ParamSpec::is_float3` is set. Audit: every `V()` factory call in the shader table maps to a float3 declaration, every `V4()` to a genuine float4.

Verified byte-for-byte against vanilla `EB_CHECKPOINTSTRUCTURE.ALO`'s `MeshBumpColorize` material chunk:

| Field | Vanilla size | Our output |
|---|---|---|
| Emissive (FLOAT4) | 29 | 29 |
| Diffuse (FLOAT4) | 28 | 28 |
| Specular (FLOAT4) | 29 | 29 |
| Shininess (FLOAT) | 18 | 18 |
| Colorization (FLOAT4) | 33 | 33 |
| UVOffset (FLOAT4) | 29 | 29 |

### Phase 6d — Coord-frame banner in `.export.log` ([PR #21](https://github.com/DrKnickers/max2alamo-2026/pull/21), shipped)

Every export's `.export.log` now opens with a coordinate-frame statement so future-you doesn't have to wonder:

```
max2alamo material diagnostics
==============================

Coordinate frame: Z up, -Y forward, +X right (Max-native; matches EaW engine)

top-level node count: N
...
```

The convention itself was already correct in the walker (no axis remapping; `IGameConversionManager::IGAME_MAX` preserves Max's native frame), but it lived only in source comments. Empirical confirmation came from the Executor Star Destroyer: engine bones at +Y (~+1875), bow at -Y (~-1870), so EaW expects -Y as the model's forward.

### Max-side test harness ([PR #23](https://github.com/DrKnickers/max2alamo-2026/pull/23), shipped) + integration test ([PR #26](https://github.com/DrKnickers/max2alamo-2026/pull/26))

Format-library unit tests cover the writer half (CI-runnable, no Max needed), but until now the **walker half** — IGame extraction, paramblock reads, skin modifier handling — had no automated coverage. Manual verification through the Max GUI was the only signal.

This adds a one-command runner that exercises the full pipeline through real 3ds Max via `3dsmaxbatch.exe`:

```
tests/maxscript/
  _harness.ms                       # shared MAXScript helpers
  test_static_box.ms                # one scene per file
  test_two_meshes_offset.ms
  test_bumpcolorize_params.ms
  test_bone_hierarchy.ms
  test_skinned_cylinder.ms
  test_skinned_rskin.ms             # PR #26 integration
  verify/
    _alo.py                         # read-only chunk-tree parser
    verify_test_<name>.py           # one assertion script per test
scripts/
  run-max-tests.ps1                 # discover + dispatch + report
```

`powershell -File scripts/run-max-tests.ps1` discovers every `tests/maxscript/test_*.ms`, runs each through 3dsmaxbatch when its cached output is stale (or the installed `.dle` is newer), dispatches the paired Python verifier, and reports pass/fail. Timestamp-based caching saves the ~25-second Max boot per test on no-op runs.

Current coverage (25 tests, all green):

| Test | Pins behaviour from |
|---|---|
| `test_static_box` | Phase 4 baseline (Root + per-mesh bone, 144B vertex layout, Standard fallback to `MeshAlpha.fx`) |
| `test_two_meshes_offset` | Phase 4c-fix bone WorldTM bake |
| `test_bumpcolorize_params` | Phase 6c DXMaterial ParamBlock extraction + float3 alpha=0 |
| `test_bone_hierarchy` | Phase 5a real bone hierarchy + local-to-parent matrices |
| `test_skinned_cylinder` | Phase 5b single-bone skinning via IGameSkin |
| `test_skinned_rskin` | Integration: 5a + 5b + 6a + 6c all firing on one mesh with `RSkinBumpColorize.fx` |
| `test_smooth_skinned_joint` | Phase 5c multi-bone weighted skinning (smooth-painted joint, 50/50 split, normalized) |
| `test_alamo_user_props` | Phase 5d `Alamo_*` user-prop round-trip (export-geometry opt-out, hidden, collision, billboard mode) |
| `test_helper_as_bone` | Phase 5e helpers-as-bones (`Alamo_Export_Transform` on a Dummy promotes it to a skeleton bone; unmarked helpers stay scene-only) |
| `test_biped_skinned` | Empirical confirmation that Max Biped sub-bones reach the walker as `IGAME_BONE` and round-trip through the full skeleton + skin pipeline (36-bone biped, cylinder skinned to Spine/Head) |
| `test_omni_light_basic` | Phase 7b.1 baseline — single OmniLight, distinctive color/intensity/atten, synth bone at world TM |
| `test_omni_pure_primaries` | Phase 7b.1 multi-light — 3 Omnis with pure R/G/B colors, monotonic connection indices |
| `test_omni_atten_disabled` | Phase 7b.1 robust-defaults — `useFarAtten=false` doesn't crash; atten fields non-negative |
| `test_directional_light` | Phase 7b.1 — DirectionalLight → type=1 |
| `test_mesh_and_omni_mixed` | Phase 7b.1 ordering — light authored first in Max; walker still emits mesh-then-light in connections |
| `test_omni_hidden_in_max` | Phase 7b.1 visibility — hidden Max light still exports; bone has `visible=False` |
| `test_spotlight_with_target` | Phase 7b.2 baseline — TargetSpot + `.Target` sibling bone; cone radian conversion; target world TM read at export time |
| `test_freespot_no_target` | Phase 7b.2 — FreeSpot exports as type=2 without a `.Target` sibling bone |
| `test_proxy_basic` | Phase 7c.2 baseline — one Alamo_Proxy at a distinctive position; default flags suppressed |
| `test_dummy_not_a_proxy` | Phase 7c.2 — plain Dummy named `p_smoke` does NOT export as a proxy (Class_ID detection, not name-prefix) |
| `test_proxy_with_flags` | Phase 7c.2 — 4 proxies x 2 optional flags (`Alamo_Geometry_Hidden`, `Alamo_Alt_Decrease_Stay_Hidden`) round-trip |
| `test_proxy_mesh_light_combined` | Phase 7c.2 — mesh + light + proxies coexist; connection table covers mesh + light only, proxies have their own 0x603 chunks |
| `test_proxy_mutex_helper_as_bone` | Phase 7c.2 — Alamo_Proxy detection wins over `Alamo_Export_Transform`; non-proxy helpers still respect Phase 5e |
| `test_phase7_acceptance` | Phase 7d — multi-feature acceptance: skinned cylinder + 2-bone chain + helper-as-bone + static box + Omni + TargetSpot + 2 proxies in one scene; **31 interaction-shaped assertions** across 8 groups (skeleton composition, mutex enforcement, mesh wiring, lights, proxies, connection accounting, bone-matrix orthonormality, `.export.log` cross-check) |
| `test_rotation_keyframes` | Phase 8b — single bone keyframed 0°→90° about Z over 31 frames; emits sibling `.ala` with FoC framing + `0x1009` rotation pool. **17-assertion verifier** covers chunk structure, FoC mini-chunks, pool size, idx_rotation per bone, frame[0] ≈ identity / frame[30] ≈ 90°-Z within 1e-3, unit-length quats, sign-canonicalisation continuity |

The harness is **not CI-runnable** (needs Max install + license seat). It's an on-demand local tool; CI keeps the format-library tests.

### Phase 5a — Real bone hierarchy walk ([PR #24](https://github.com/DrKnickers/max2alamo-2026/pull/24), shipped)

Structural foundation for skinned export. Adds a bone-hierarchy pass that runs BEFORE the mesh walk in `walk_scene`, populating `ExportScene.bones` with real `IGAME_BONE` nodes carrying real `parent_index` links (pointing at the nearest exportable-bone ancestor, or 0 = synthetic Root) and local-to-parent matrices via `IGameNode::GetLocalTM()` — matching vanilla `0x206` chunks (confirmed against `AI_DACTILLION.ALO`).

Index ordering after the new pass:

```
[0]     synthetic Root sentinel (parent = 0xFFFFFFFF)
[1..N]  real Max bones (parent links real, local-to-parent matrices)
[N+1..] synthetic per-mesh attachment bones (parent = Root, world TM)
```

Scope: `IGAME_BONE` only. `IGAME_BIPED` subtypes and helpers tagged `Alamo_Export_Transform` are Phase 5d. Meshes still emit synthetic per-mesh attachment bones regardless of whether they have a Skin modifier — that gets reworked in Phase 5b for skinned meshes.

The `test_bone_hierarchy` regression specifically discriminates local-vs-world: a 3-bone chain along world +Y produces 25-unit local-offset translations between successive bones. If any future refactor regresses to `GetWorldTM`, B_Tip would show translation magnitude 50 (world position) instead of 25 (local-to-parent offset); the verifier prints a pointed diagnostic in that case.

### Phase 5b — Single-bone skinning via IGameSkin ([PR #25](https://github.com/DrKnickers/max2alamo-2026/pull/25), shipped)

Wires the `IGameSkin` modifier into the walker. Each face-corner vertex now reads its skin weights from the modifier, picks the **bone with the highest weight** (rigid attachment to the dominant bone), and writes that bone's index to `boneIdx[0]` with weight 1.0. Slots 1..3 stay unused; smooth multi-bone weighted deformation is Phase 5c.

Per-mesh routing:

| State | Behaviour |
|---|---|
| **Static** (no Skin modifier) | Unchanged Phase 4c. Synthetic per-mesh attachment bone parented to Root with the mesh's WorldTM baked in. `object#i → per-mesh-bone`. Every vertex's slot 0 points at the per-mesh bone. |
| **Skinned** (Skin modifier present) | **No per-mesh attachment bone.** `object#i → bone#0 (Root)`, matching vanilla content (`AI_DACTILLION.ALO`). Per-vertex slot 0 carries the dominant bone's index in the real hierarchy. |

Format-library refactor: `ExportVertex` gained per-vertex `bone_indices[4]` + `weights[4]` (default `{0,0,0,0}` / `{1,0,0,0}` = "rigidly bound to Root"). These are now the source of truth for the boneIdx/weight slots in the 144 B vertex record. `append_vertex` reads them directly; `build_vertex_chunk` and `build_submesh_geometry` simplified to match. The `bone_index` parameter passed through `build_submesh_geometry` from Phase 4c is gone.

Walker side new: `SkinContext` struct carries `IGameSkin*` + the `(INode* -> bone index)` map populated by `walk_bones` + a fallback bone index. `resolve_dominant_bone(ctx, vert_idx)` does per-vertex dispatch — picks max weight among the skin's bones, resolves the IGameNode → INode → ExportScene index via the map.

The `test_skinned_cylinder` regression validates with a 3-bone chain + skinned cylinder where each Y-row rigid-attaches to a different chain bone; the verifier asserts 4 bones (no per-mesh bone), connection to Root, per-vert `boneIdx[0] ∈ {1,2,3}` (never 0), and distribution non-degenerate (all three chain bones receive bindings). The `test_skinned_rskin` integration verifies the full Phase 5 + Phase 6 stack interacts correctly on one mesh.

### Phase 5c — Multi-bone weighted skinning (shipped)

Generalises Phase 5b's "dominant bone, weight = 1.0" path to read every (bone, weight) pair from `IGameSkin` and pack the top 4 by weight (renormalized to sum to 1.0) into the 4-slot vertex tail. Splits the logic in two:

- **Pure host-agnostic packer** in `alamo_format::skin::top4_normalized` (new `alamo_format/skin_weights.{h,cpp}`). Takes a vector of `(bone_index, weight)` pairs and a fallback bone index; returns a `VertexBinding { bone_indices[4], weights[4] }` with the top 4 by weight renormalized so the slots sum to 1.0, ties broken on `bone_index` ascending for serialization determinism. Non-positive weights are dropped defensively. CI-testable without Max — 9 new unit tests cover empty input, all-zero weights, single/three/four bones, >4 with drop+renormalize, negative-weight dropping, 50/50 tie-break, and unnormalized input.
- **Walker adapter** `resolve_multi_bone` in `scene_walker.cpp` replaces `resolve_dominant_bone`. Iterates `IGameSkin::GetNumberOfBones / GetWeight / GetIGameBone`, maps each influence's `INode*` through `walk_bones`' `bone_map` to an `ExportScene` bone index, drops un-mappable influences (rather than coalescing them into the fallback, since "bone influence pointing at something we don't export" is a missing-content signal we'd want to surface, not silently steer to the per-mesh bone), and hands the result to `top4_normalized`. Static meshes still short-circuit to a rigid binding (slot 0 = per-mesh attachment bone, weight = 1.0).

`ExportVertex::bone_indices[4]` / `weights[4]` were already in the format library (added speculatively in Phase 5b); this PR just populates slots 1..3 for skinned meshes for the first time. The 144 B writer in `alo_build.cpp` was already pass-through.

`test_smooth_skinned_joint.ms` validates: a 2-bone chain spanning a 40-unit joint, with smooth-painted weights across a 40-unit transition band (linear blend, w_B1 = clamp((Y - 20) / 40, 0, 1)). 432 face-corner vertices export with 48 joint-50/50 splits, 144 each pure-B0 / pure-B1, weights summing to 1.0 in every vertex, and at least one multi-bone influence pinning the regression to the 5b → 5c lift (a regression to single-bone would leave every `weights[1..3] == 0` and the verifier catches it).

3dsmaxbatch quirk encountered while writing the test: `skinOps.GetNumberVertices` returns 0 in batch mode even when Skin is fully attached (the modifier-panel UI state needed to populate the cache isn't entered). The existing skinned tests "passed" because Skin's envelopes auto-assign weights when no explicit painting runs, but explicit smooth weights need a real iteration. Workaround: drive the per-vertex loop from `snapshotAsMesh cyl`'s `numverts` instead. `skinOps.SetVertexWeights` itself does accept explicit vertex indices even when `GetNumberVertices` lies.

---

## Future phase plans

### Authoring UI — Alamo Utility command panel (shipped)

Faithful clone of the legacy Petroglyph max2alamo Utility UI: three rollouts that appear under the command panel's Utilities tab > More... > Alamo Utility, matching the original Max 8/9 plugin screenshot pixel-for-pixel where dialog units allow.

**Why it lives separately from `AloExport`:** Max plugins can expose multiple class types from one `.dle`; SceneExport drives File → Export, while `UtilityObj` drives the command-panel rollouts. The same DLL exposes both via `LibClassDesc` indexing, so users get one install path. The Utility has its own stable `Class_ID(0x6ed3a4f1, 0x4f51ab63)` (distinct from the SceneExport's) — once shipped this must never change since Max persists Utility-class references in scenes that have ever opened the panel.

| Rollout | Controls | Behaviour |
|---|---|---|
| Node Export Options | `Export Transform`, `Is "Extra" Bone`, Billboarding radios (Disable / Parallel / Face / ZAxis View / ZAxis Light / ZAxis Wind / Sunlight Glow / Sun) + help button, `Export Geometry`, `Enable Collision`, `Hidden`, `Alt Dec Stay Hidden` | Toggles ↔ `Alamo_*` user properties on the currently-selected INode. Selection change refreshes state. Parent/child enable-disable matches the screenshot (Is-Extra-Bone gated by Export Transform; collision/hidden/alt-dec gated by Export Geometry; billboard radios gated by Export Transform). |
| Quick Selection Utility | `Export Transform`, `Export Geometry`, `Enable Collision` buttons + LOD/Alt spinners with help | Each button selects every node in the scene whose corresponding property is set. LOD/Alt spinners select every node whose `Alamo_LOD` / `Alamo_Alt` matches the spinner value. |
| Animation Settings | Anim Name combo (seeded with `-- none --`), Start/End spinners, `<<` `Add` `Del` `>>` nav buttons, `Display Current` / `Display All` | UI present but disabled — the named-clip backend lands with the Phase 8 `.ala` writer. Visual fidelity is the goal here so the panel matches the screenshot. |

Implementation: `max2alamo/src/alamo_utility.{h,cpp}` (UtilityObj subclass, ClassDesc, three DLGPROCs + helpers); `max2alamo/resources/utility_dialogs.rc` (three dialog templates, 108 dialog-units wide = standard command-panel column); `max2alamo/resources/utility_resource.h` (control IDs). `plugin_entry.cpp` bumped `LibNumberClasses` from 1 to 2 and routes index 1 to the new ClassDesc.

The walker still ignores `Alamo_*` user props on read at export time — that's Phase 5d work. Until then the panel writes the props but nothing downstream consumes them (other than the `Alamo_Shader_Name` override which has been read since Phase 4c).

### Phase 5d — `Alamo_*` user-property family, simple-flag half (shipped)

Walker-side read of the props the Utility panel writes that have a direct 1:1 mapping into existing `ExportBone` / `ExportMesh` fields — no architectural changes needed. Four props landed in this PR:

| Property | Walker site | Behaviour |
|---|---|---|
| `Alamo_Export_Geometry` | `walk_node` (mesh path) | When **explicitly false**, skip the mesh entirely (recurse into children for free-standing groups, then return). Absent prop ⇒ export, preserving the pre-5d "everything is exported" contract that the existing test corpus depends on. |
| `Alamo_Geometry_Hidden` | `build_mesh` | When present, overrides `ExportMesh::is_hidden` regardless of Max's own `IsNodeHidden()`. Falls back to Max-native hidden state when absent. |
| `Alamo_Collision_Enabled` | `build_mesh` | Sets `ExportMesh::is_collision`. Defaults to false (collision-disabled) when absent. |
| `Alamo_Billboard_Mode` | `walk_bones` (real bones) + `walk_node` (static-mesh synth bone path) | Read as `int` and stored in `ExportBone::billboard_mode`. For static meshes the prop lives on the mesh node and propagates to the synthetic per-mesh attachment bone (which is what the engine animates for billboarding). |

Plumbing: three new typed helpers in `scene_walker.cpp` (`read_node_user_prop_bool`, `read_node_user_prop_int`, `has_node_user_prop`) sitting next to the existing `read_node_user_prop`. Property-key constants moved to a single block near `kShaderOverrideKey` and named to match `alamo_utility.cpp`'s `kProp*` constants byte-for-byte (so the read side here always sees what the write side there stored).

Verifier extension: `_alo.py`'s `Mesh` dataclass gained `is_hidden` + `is_collision` populated from the `0x402` chunk's offsets 32/36, so any test going forward can assert on these flags.

`test_alamo_user_props.ms` regression: four boxes, one per code path — `PlainBox` (no props, all defaults), `HiddenColl` (both flags true), `BillboardFace` (mode=2), `SkippedMesh` (Export_Geometry=false). Verifier checks each round-trip end-to-end through the walker.

### Phase 5e — helpers-as-bones (shipped)

`is_exportable_bone` now accepts `IGAME_HELPER` nodes (Dummy / Point / Arrow) in addition to `IGAME_BONE`, gated on the `Alamo_Export_Transform` user property. Opt-in by design: real Max bones still auto-export (preserving the corpus contract from Phase 5a), but helpers only get promoted to the skeleton when the modder explicitly marks them — most scenes have helpers that aren't meant to ship (look-at targets, authoring rigs, reference markers).

All the existing 5a plumbing (local-to-parent matrices via `GetLocalTM`, parent-index propagation, the `bone_map` for skin resolution) works unchanged because the relevant `IGameNode` methods are type-agnostic. The walker change is a single switch in `is_exportable_bone` plus a new property-key constant.

Helper nodes that the user doesn't opt in stay scene-only — their children, including real bones, still get correct `parent_index` values because `walk_bones` already does "nearest exportable-ancestor" tracking (Phase 5a).

`test_helper_as_bone` regression: one Dummy with `Alamo_Export_Transform=true` (appears in skeleton with its position preserved as the local-TM translation), one Dummy with no prop (absent from skeleton), one static Box (exports normally — proves the helper pass doesn't disturb the mesh path).

### Phase 5f — remaining `Alamo_*` props research (shipped, research-only)

Format research into the four `Alamo_*` props the Utility panel writes that Phase 5d/5e didn't cover. Sources: Mike Lankamp's `alamo2max.ms` (.alo/.ala reader, treated as ground truth), Gaukler's [Blender ALAMO plugin](https://github.com/Gaukler/Blender-ALAMO-Plugin) (`settings.py`), and our reader's coverage of the 0x205/0x206 bone chunks.

| Property | On-disk representation | Walker action this phase |
|---|---|---|
| `Alamo_Is_Extra_Bone` | **None.** Doesn't appear in Mike's reader or Gaukler's plugin. Pure Max-side authoring marker — the legacy PG plugin used it internally (likely to suppress emission of a `0x602` connection so the bone exports as animation-only / lookat-target), but the resulting `.alo` has no flag for "extra-ness." | No-op. Could be inferred from omitted-connection patterns in a corpus dump, but not needed for v1. |
| `Alamo_LOD` | **None.** No binary representation. Consumed by the Utility panel's Quick Selection rollout (filter selected nodes by integer LOD value — already shipped in 5d's UI) and possibly by file-naming (`<model>_LOD1.alo`) at the legacy plugin's export time. | No-op. Filename-suffix support can land with Phase 9 polish if user feedback wants it. |
| `Alamo_Alt` | **None.** Same status as `Alamo_LOD`. No binary representation; UI filter only. | No-op. |
| `Alamo_Alt_Decrease_Stay_Hidden` | **Proxy chunk (0x603) field.** Read by `alamo2max.ms:604` inside mini-chunk type `8` as `reader.GetLong() != 0`. Only meaningful when the node ends up as a proxy (HP_* convention etc.). For non-proxy nodes Mike's importer writes `false` as a default. | **Deferred to Phase 7** (lights / hardpoints / proxies), where the 0x603 chunk gets implemented. |

Verified along the way: our writer always emits `0x206` bone chunks (Phase 4c convention; vanilla has 44653 × 0x206 vs 18 × 0x205, so we match the dominant pattern). 0x205 is just 0x206 minus the billboard_mode u32; both layouts share the 12-float matrix. No behaviour change needed.

**Conclusion:** Three of the four props have nothing for the walker to do today. The fourth is a proxy-chunk field that belongs with the proxy work in Phase 7. Closing out the Phase 5 series here.

### Phase 7 — Lights + proxies (shipped)

Format library: `0x1300` light containers (`0x1301` name + `0x1302` data: type, RGB, intensity, atten, hotspot, falloff) and `0x603` proxy chunks (mini 5/6/7/8). Walker: `walk_lights` emits per-light synth bones + ExportLight from IGAME_LIGHT (Omni/Directional/Spotlight, with `.Target` sibling bone for TargetSpot); `walk_proxies` detects helpers by `Class_ID == kAlamoProxyClassID` (not name prefix — proxies are explicit `Alamo_Proxy` instances, registered in 7c.1).

Closed across 7a (format-lib builders + 9 unit tests), 7b.1/7b.2 (lights walker + 6 harness tests), 7c.1 (helper plugin class registered), 7c.2 (proxies walker + 5 harness tests), and 7d (full integration acceptance).

**In-game acceptance** (Tier 4) — a real fighter exported with `Alamo_Proxy` hardpoint helpers firing weapons in EaW — is the remaining proof outside the harness. Deferred to Phase 8/9 when a full asset is available; the harness coverage is sufficient to land Phase 7 as shipped.

### Phase 7d — Full integration acceptance (shipped)

One new harness test — `test_phase7_acceptance` + `verify_test_phase7_acceptance.py` — that authors a single Max scene combining every Phase 4..7 surface (skinned cylinder over a 2-bone chain, helper-as-bone, static box, Omni light, TargetSpot with `.Target` sibling, two `Alamo_Proxy` helpers with and without `Alamo_Geometry_Hidden`) and asserts the interaction invariants no per-feature test can cover. Scope is deliberately measurement-only: no walker or format-library changes, no new Tier 1 invariants, no replication of per-feature deep-dives.

Scene shape on disk: **10 bones** (Root + B0/B1 + ExportedPivot + StaticBox + OmniMain + SpotMain + SpotMain.Target + p_alpha + p_beta), **2 meshes** (skinned `SkinCyl` connecting to Root, static `StaticBox` connecting to its own attachment bone), **2 lights** (Omni + Spotlight; specifically no Directional), **2 proxies** (mini 5+6 for p_alpha, mini 5+6+7 for p_beta), **4 connections** (meshes + lights only; proxies live in `0x603`).

Verifier covers **31 assertions across 8 groups**:

- **A** — Skeleton composition + topology (#1–#6)
- **B** — Mutex enforcement under load (#7–#9): proxy/light names appear exactly once in bones; ExportedPivot doesn't leak into lights/proxies/meshes
- **C** — Mesh wiring (#10–#15): skinned-to-Root vs static-to-attachment; multi-bone influence on the joint; tightened weight-sum precision (1e-4); NaN/Inf sweep
- **D** — Lights (#16–#20): type set `[0, 2]`; `SpotMain.Target` at the target's authored position; cone in radians with `hotspot ≤ falloff ≤ π`
- **E** — Proxies (#21–#24): flag round-trip; synthetic-bone naming invariant; proxies *not* in the connection table
- **F** — Connection table accounting (#25–#28): count = 4; object_index permutation; bone_index resolves; only `SkinCyl` allowed to connect to Root
- **G** — Bone-matrix numeric correctness (#29–#30): authored-position round-trip within 1e-3 for all 7 source nodes; orthonormality (unit columns, pairwise orthogonal, positive determinant)
- **H** — Cross-source consistency (#31): `.export.log` summary counts and flag values agree with parsed binary

Verification layers run before sign-off:

1. **Tier 1 strict** (`validate_alo.py`) — clean.
2. **Tier 2 round-trip** (`alo_roundtrip.exe`) — byte-identical, 70709 bytes.
3. **Tier 3 bespoke verifier** — 31/31 assertions pass.
4. **3× determinism** — three back-to-back Max boots produce identical SHA-256 `56FCEAF02E3D043E46A4BB90818EC22C9A814D0E55CC94D27A6464961A789CC3`.
5. **`alo_dump` stability** — three runs produce identical content (post-header).
6. **Full harness 24/24, format-lib unit tests 60/60, corpus sweep 2066/2066.**
7. **Negative tripwires** — three deliberate breakages (drop hidden flag, rename a bone, move a light) each fire their expected single assertion (#22, #1, #29). Confirms the verifier actually bites.

The tripwires are documented but not committed — they live only as a dev-time validation step. The acceptance test stands on its own as a regression guard against any future change that breaks the multi-feature interaction surface.

### Phase 6e (optional polish) — Walker-side `.export.log` consistency

`.export.log` currently dumps params with their walker-side values (Max's `TYPE_FRGBA` returns alpha=1). The on-disk bytes correctly apply the Phase 6c float3 alpha-zero convention before writing, so a quick reader of the log might see `Emissive = (0, 0, 0, 1)` and think the bytes say the same. Cheap fix: apply the same `is_float3` zero-out at log time, so the diagnostic mirrors the bytes. Pure clarity; no functional change.

### Phase 8 — Animation export incl. visibility tracks (in progress)

The `.ala` writer. Five sub-phases per the Phase 8 plan: 8a typed format-library pipeline, 8b walker rotation pass, 8c translation+scale + FoC pool packing, 8d visibility tracks (first-class v1 goal), 8e full integration acceptance. Each ships as a separate PR.

Animation Settings UI wiring is **explicitly deferred to Phase 9**; v1 reads `Alamo_Anim_Start` / `Alamo_Anim_End` / `Alamo_Anim_Name` from the scene-root user properties.

Acceptance: a walk cycle plus a scene with animated visibility (e.g. blinking nav lights) export and play correctly via Mike's importer and in EaW.

### Phase 8a — Typed `.ala` read/write pipeline (shipped)

`alamo_format/include/alamo_format/ala_anim.h` defines `AlaAnimation` + `AlaBoneTrack` with both typed fields (so the Phase 8b+ walker can populate them directly) and raw payload preservation per leaf (so a `read_ala` → `AlaAnimation` → `build_ala` round-trip is identity-preserving by construction). `build_ala()` synthesises canonical FoC bytes from typed fields when raw payloads are empty; vanilla corpus round-trip exercises the pass-through path.

**Acceptance signal — 2863/2863 vanilla `.ala` byte-identical via the typed pipeline.** That exceeds the plan's parse-clean target for the 1363 EaW files, because the design carries each per-bone 0x1004/0x1005/0x1006/0x1007/0x1008 leaf as an opaque `track_leaves` ChunkNode that round-trips verbatim regardless of format.

Format details, all corpus-confirmed:
- Top-level: single `0x1000` container.
- `0x1001` (info): mini-chunks 1 (nFrames, u32), 2 (fps, f32), 3 (nBones, u32). FoC also has 11 / 12 / 13 (rotation / translation / scale word counts). Presence of any FoC mini-chunk is the format-detection signal.
- `0x1002` (per-bone): contains `0x1003` (bone info — mini-chunks 4–9 and FoC's 14–17) plus any track leaves (0x1004/5/6/7/8).
- File-scope FoC pools after the last `0x1002`: **`0x100a` (translation) BEFORE `0x1009` (rotation)** — confirmed against `EI_DARKTROOPER_ONE_WALKMOVE_00.ALA` at offsets 2870 / 3568.
- Pool data is a flat little-endian `int16` stream of size `n_*_words × n_frames × component_count`.

Verification layers (per the rigorous-testing convention):
- 81/81 Catch2 unit tests (60 pre-existing + 21 new), covering struct synth, byte-identity round-trip of synthesised inputs, FoC detection, EaW path, pool LE encoding, empty-animation edges, malformed-input throws.
- 1500/1500 FoC + 1363/1363 EaW corpus byte-identical via `ala_typed_roundtrip` CLI.
- 4929/4929 `alo_roundtrip` chunk-tree round-trip unchanged.
- 2066/2066 `.alo` corpus sweep unchanged.
- 3× full-corpus determinism — identical pass counts every run.
- `alo_dump` output identical across runs for 3 representative files (post-header).
- 3 negative tripwires: corrupted top-level chunk ID → reader throws; swapped pool emission order → byte diff at offset 2870 (chunk ID `0A 10` vs `09 10`); mutated test assertion → ctest reports failure on the expected test. All reverted after demonstration.

### Phase 8b — Walker rotation pass (shipped)

Adds `walk_animation()` as a fifth pass after `walk_proxies` in `max2alamo/src/scene_walker.cpp`. The walker reads `Alamo_Anim_Start` / `Alamo_Anim_End` / `Alamo_Anim_Name` user properties from the scene-root node (via `Interface::GetRootNode()`), then for every bone in `bone_map` (real Max bones + Phase 5e helpers-as-bones) samples `IGameNode::GetLocalTM(t)` per frame, extracts rotation-only (drops scale by normalising the 3×3 columns to unit length), converts to `Quat`, and packs to int16 XYZW with sign canonicalisation against the previous frame (so the controller's quat-hemisphere flips don't produce playback twists).

`walk_scene` signature extended to `bool walk_scene(Interface*, ExportScene&, AlaAnimation&, std::string&)`. `DoExport()` in `alo_export.cpp` writes a sibling `<name>.ala` after the `.alo` when `AlaAnimation` has at least one bone with `idx_rotation >= 0`. Existing 24 harness tests don't author `Alamo_Anim_*`, so they produce only `.alo` — no spurious `.ala`. The `.export.log` gains an "Animation:" line when a `.ala` is emitted.

`scripts/run-max-tests.ps1` gains a Tier 2-ala step: after the `.alo` tiers, if a sibling `.ala` exists, `ala_typed_roundtrip` is invoked on it. Skipped silently when no `.ala` is present.

Out of 8b scope (deferred to 8c/8d): translation tracks (`0x100a`), scale tracks, visibility tracks (`0x1007`), FoC pool deduplication.

Pinned signals:
- New harness test `test_rotation_keyframes` (single bone, 0°→90° about Z over 31 frames). 17-assertion verifier covers chunk structure, FoC framing, pool size = `n_frames × n_rot_words × 2`, idx_rotation = 0 on TestBone / -1 on others, frame[0] ≈ identity within 1e-3, frame[30] ≈ `(0, 0, 0.7071, 0.7071)` within 1e-3, unit-length quats within 1e-2, sign-canonicalisation continuity.
- Full harness: 25/25. Existing 24 produce only `.alo` (no spurious siblings).
- Byte-identity of existing exports unchanged after plugin rebuild — verified by re-exporting `test_phase7_acceptance` and confirming SHA `56FCEAF0…` matches the pre-8b cache.
- 3× byte-identical determinism on the new test (`.alo` SHA `3791F274…`, `.ala` SHA `2B60515B…`).
- Non-regression: 4929/4929 `alo_roundtrip` · 2066/2066 `.alo` sweep · 2863/2863 `.ala` typed-pipeline · 81/81 unit.
- Three negative tripwires: drop the `animate on` block → frame quats wrong (#13/#14); change `Alamo_Anim_End` to 15 → `n_frames = 16` (#3); corrupt the writer to emit `n_translation_words = 3` → assertion #7 fails. All reverted post-demonstration.

### Phase 9 — Polish + v1 release

Export options dialog (frame range, format-version flag, log path), structured warnings/errors panel, logging to file, README polish. Tag `v1.0`. **Publish the built `.dle` as a GitHub Releases artifact** — this is how users get the plugin since CI cannot build it.

---

## Open format questions

Tracked at the bottom of [`docs/format-notes.md`](format-notes.md). Most Phase 4-relevant questions are now resolved; remaining:
- `0x402` bbox layout (6 floats min/max vs other?). Cosmetic; doesn't block rendering.
- `0x10002` vertex-format chunk payload contents.
- Collision tree (`0x1200`-`0x1203`) internal structure (only blocks Phase 5+ if collision export is required).

---

## Quick command cheat sheet

```powershell
# Configure + build everything
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build --config Release --parallel

# Run unit tests
ctest --test-dir build --build-config Release --output-on-failure

# Round-trip the entire local corpus
build\tools\alo_roundtrip\Release\alo_roundtrip.exe --dir tests\corpus

# Dump a single .alo
build\tools\alo_dump\Release\alo_dump.exe path\to\file.alo

# Build only the Max plugin (after configure). A POST_BUILD step drops
# the .dle into <repo>\plugin\max2alamo.dle automatically; Max is
# configured to scan that folder via Customize -> Configure System
# Paths -> 3rd Party Plug-Ins. No copy step, no UAC. See docs/build.md.
cmake --build build --config Release --target max2alamo

# Run the Max-side end-to-end regression suite (needs Max + license).
# Re-exports only stale tests; ~25 sec per test that needs re-running.
powershell -File scripts/run-max-tests.ps1

# Run a single Max-side test, forcing re-export.
powershell -File scripts/run-max-tests.ps1 -Filter test_skinned_* -Force

# Regenerate the Effects11 stub shaders from the manifest.
python scripts/generate-max-preview-stubs.py

# Synthesize an .alo from a hard-coded ExportScene (sanity check the writer
# without launching Max).
build\tools\alo_synth\Release\alo_synth.exe path\to\test.alo

# Renew interaction limits manually
bash scripts/renew-interaction-limits.sh

# Open a PR after pushing a feature branch
gh pr create --base main --title "..." --body "..."

# After merge: sync local main + delete branch
git checkout main && git pull && git branch -d <branch>
```

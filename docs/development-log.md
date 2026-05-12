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
| 5 | Skeleton + skinning | ⏭ next | 4-bone-weighted skinned mesh round-trips through Mike's importer |
| 6 | Materials + shader parameters | pending | Top 5 Petroglyph shaders export with correct params |
| 7 | Lights, hardpoints, proxies | pending | Fighter exports with working hardpoints in EaW |
| 8 | Animation export incl. visibility tracks | pending | Walk cycle + animated visibility play correctly in-game |
| 9 | Polish + v1 release | pending | v1.0 tag with `.dle` published via GitHub Releases |

Each phase has a corresponding GitHub issue (`#1` for Phase 0.5 through `#10` for Phase 9). Closed issues = shipped phases.

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
| Format library headers | `alamo_format/include/alamo_format/` |
| Format library sources | `alamo_format/src/` |
| Format library tests | `alamo_format/tests/` (Catch2 v3 via FetchContent) |
| Standalone CLIs | `tools/alo_dump/`, `tools/alo_roundtrip/` |
| Max plugin sources | `max2alamo/src/` |
| Max plugin resources | `max2alamo/resources/` (.def, .rc) |
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

---

## Future phase plans

### Phase 5 — Skeleton + skinning (next)

Real bones — replace Phase 4's per-mesh synthetic attachment bones with the actual Max hierarchy. Pre-staged work: Phase 4 already established the per-mesh bone convention; Phase 5 puts a real skeleton + multi-bone vertex weights in place of the single-bone-per-vertex Phase 4 default.

Rough scope:
- Bone discovery (Max bones / Biped / dummies tagged `Alamo_Export_Transform` user property). Build the hierarchy in `ExportScene.bones` with real parent indices.
- Bake each bone's Max world transform into the column-major `matrix` (Phase 4 left this as identity).
- Skin-modifier extraction: per-vertex up to 4 (boneIdx, weight) tuples. Layout in the existing 144-byte vertex chunk: boneIdx in offsets 112..128, weights in 128..144 (we already write `[bone_index, 0, 0, 0]` / `[1, 0, 0, 0]` for the single-bone Phase 4 case; multi-bone fills slots 1..3 with real data).
- `Alamo_Billboard_Mode` user prop → emit `0x206` (with billboard) instead of `0x205` (we already always emit `0x206` with billboard=0; just need to read the prop).
- Connections section update: mesh attaches to whichever bone holds it in Max's hierarchy, not to the synthetic per-mesh bone.

Acceptance: a 4-bone-weighted skinned mesh exported from Max imports correctly via Mike Lankamp's importer (deformations match in-Max preview); animated turret on a fighter rotates correctly in-game in EaW.

Likely Phase 5 risks:
- Vertex-to-bone mapping: our current scheme assumes each mesh has one bone. Real skinning has 4 bones per vertex with varying weights — easy to introduce off-by-one errors in the boneIdx mapping. Mitigation: round-trip a Mike-imported vanilla rigged model (e.g. an X-wing's S-foils or a stormtrooper) — same correctness oracle pattern Phase 1/2 used.
- Coordinate frame: Phase 4's identity transforms aren't a stress test. Real Max hierarchies have non-trivial parent-child relationships, rotation, scale. Verify with an asymmetric multi-bone test scene.

### Phase 6 — Materials + shader parameters

Port `material_parameter_dict`, `vertex_format_dict`, `bumpMappingList` from `Blender-ALAMO-Plugin/io_alamo_tools/settings.py` into a data-driven `shader_table.h`. Implement chunks `0x10101` (shader name) + `0x10102`–`0x10106` (INT, FLOAT, FLOAT3, FLOAT4, TEXTURE param mini-chunks).

Map Max DirectX Shader material → Alamo shader + params with full fidelity. Map Standard material → a documented default (`MeshBumpColorize.fx` or similar) with sensible defaults, surfacing a warning when info is lost.

Acceptance: top 5 Petroglyph shaders (`MeshBumpColorize.fx`, `MeshBumpSpecular.fx`, `RSkinBumpColorize.fx`, `MeshShadowVolume.fx`, `MeshCollision.fx` or similar) export with full parameter fidelity. Verified by exporting then comparing in-game render to a vanilla model using the same shader.

### Phase 7 — Lights, hardpoints, proxies

`0x1300` light containers (with `0x1301` name + `0x1302` data: type, RGB, intensity, atten, hotspot, falloff). Helper-object naming convention: any non-mesh, non-bone Max dummy named `HP_*` (or any helper marked as a proxy via convention) becomes a `0x603` proxy chunk in connections.

Acceptance: a fighter exported with `HP_Weapon_*` dummies has working hardpoints in EaW (weapons fire from the correct positions in-game).

### Phase 8 — Animation export incl. visibility tracks

The `.ala` writer. Sample Max bone tracks per frame in the user-specified range. Quaternion compression (`int16 = round(q * 32767)`, XYZW order). Position / scale compression (per-bone offset + scale, then per-frame `int16[3]`). Default to **FoC track-pool format** (smaller, both engines support it).

**Object visibility tracks (first-class v1 goal):** sample each animated object's visibility per frame, encode as bit-packed track (`'1'` visible, `'0'` hidden), each byte bit-reversed for little-endian. Only emit `0x1007` for bones where any frame is hidden.

Acceptance: a walk cycle plus a scene with animated visibility (e.g. blinking nav lights) export and play correctly via Mike's importer and in EaW.

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

# Build only the Max plugin (after configure)
cmake --build build --config Release --target max2alamo

# Install plugin into Max (requires elevation)
Copy-Item build\max2alamo\Release\max2alamo.dle `
    "C:\Program Files\Autodesk\3ds Max 2026\Plugins\"

# Renew interaction limits manually
bash scripts/renew-interaction-limits.sh

# Open a PR after pushing a feature branch
gh pr create --base main --title "..." --body "..."

# After merge: sync local main + delete branch
git checkout main && git pull && git branch -d <branch>
```

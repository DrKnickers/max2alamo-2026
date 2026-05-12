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
| 4 | Static geometry export | ⏭ next | Textured cube exported from Max renders in EaW |
| 5 | Skeleton + skinning | pending | 4-bone-weighted skinned mesh round-trips through Mike's importer |
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

---

## Future phase plans

### Phase 4 — Static geometry export (next)

Make the plugin actually write a real `.alo` from a Max scene's static meshes.

Rough scope:
- IGame traversal (`IGameScene` → `IGameNode` → `IGameMesh`). Material count + per-material vertex/index buffers.
- Geometry → `ChunkNode` tree using the `alamo_format` writer. Chunks: `0x200` skeleton (placeholder root only for now), `0x400` mesh, `0x402` mesh metadata, `0x10100` material container, `0x10001` sizes, `0x10002` vertex format, `0x10004` faces, `0x10005` or `0x10007` vertices.
- For Phase 4 use `alD3dVertNU2` (pos + normal + UV, no skin, no tangents) to keep the surface narrow. Tangent / bumpy formats arrive in Phase 6.
- Default-shader assignment: `alDefault.fx` or `MeshBumpColorize.fx` with one diffuse texture from the Max material's diffuse map slot.
- Bounding box in `0x402`.

Acceptance: a textured cube exported from Max loads in [AloViewer](https://modtools.petrolution.net/tools/AloViewer) and renders correctly in EaW.

Where Phase 4 will likely need design iteration:
- Scene-walk strategy (which Max nodes count as exportable meshes — respect `Alamo_Export_Geometry` user prop).
- Coordinate-system handling (working assumption: identity for vertex data, UV V flipped to `1 - v`; verify against an imported-then-re-exported vanilla model).
- Material → shader inference for Standard materials (vs explicit DirectX Shader material).

### Phase 5 — Skeleton + skinning

Bone discovery (Max bones / Biped / dummies tagged `Alamo_Export_Transform`), hierarchy export (`0x202` containers with `0x203` name + `0x205` or `0x206` data), Skin modifier extraction. **Up to 4 bone influences per vertex** (B4I4 layout); Phase 0.5 RE work pinned this down.

`Alamo_Billboard_Mode` user prop → emit `0x206` (with billboard) instead of `0x205`. Connections section (`0x602`) for object → bone attachment.

Acceptance: a 4-bone-weighted skinned mesh exported from Max imports correctly via Mike Lankamp's importer (deformations match in-Max preview).

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

Tracked at the bottom of [`docs/format-notes.md`](format-notes.md). Currently:
- Exact byte layouts of `RSkin*` and non-skinned vertex formats (verify Phase 4 by dividing vertex chunk size by `vertexCount`).
- `0x402` bbox layout (6 floats min/max vs other?).
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

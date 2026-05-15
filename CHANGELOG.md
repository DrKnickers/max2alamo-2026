# Changelog

All notable changes to this project are documented in this file. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions adhere to [semver](https://semver.org/).

Releases are tagged on `main` as `vMAJOR.MINOR.PATCH` and published as GitHub Releases with the `alamo_format` library, the CLIs (`alo_dump`, `alo_roundtrip`), and a locally-built `.dle` Max plugin attached.

## [Unreleased]

(no changes since v0.9.0)

## [0.9.0] — 2026-05-15

First pre-1.0 release. Signals "format library + Max plugin feature-complete, awaiting v1.0 commitment after a stabilization window." All phase work since project inception is captured below; subsequent releases will follow a one-section-per-release rhythm.

### Added — format library (phases 0 → 8a, 11b.1)

- `alamo_format` static C++17 library (no Max-SDK dependency). Reads and writes `.alo` model files and `.ala` animation files byte-identical against the entire vanilla EaW + FoC corpus (2,066 / 2,066).
- Typed read/write pipeline (`build_alo` / `build_ala` + chunk-tree reader/writer pair).
- Per-shader parameter spec table (`shader_table.h`) covering all 39 vanilla Petroglyph shaders.
- Skinning weights helper (`skin_weights.h`): top-4-by-weight selection + renormalization.
- Multi-clip animation support: `AlaAnimation::name` round-trips; one `.ala` per clip with filename `<basename>_<clipname>.ala` matching Mike Lankamp's importer's auto-discovery glob.
- Legacy clip-data scanner (`legacy_clip_scan.h`) for translating legacy Petroglyph `.max` AlamoUtility appData records to Phase 11b's user-prop convention.
- Shadow-volume closed-manifold validator (`shadow_volume_check.h`).
- CLI: `alo_dump` -- dump chunk tree of any `.alo` / `.ala` file; `alo_roundtrip` -- byte-identical re-serialisation oracle.

### Added — 3ds Max 2026 plugin (phases 3 → 12.1)

- `max2alamo.dle` Scene Exporter plugin for 3ds Max 2026.
- Static mesh export with per-vertex normals + tangents + UVs (Phases 4, 6b).
- Per-material shader stub generation: 39 `.fx` files under `shaders/max-preview/` covering the full Petroglyph shader set (Phase 6a).
- Per-material parameter export including DirectX Shader material recursion + `Alamo_Shader_Name` user-prop override (Phase 6c).
- Skeletal animation: bone hierarchy, multi-bone weighted skinning, rotation + translation tracks, visibility tracks (Phases 5a-c, 8a-d).
- Lights: Omni, Directional, Spotlight + `.Target` sibling bone (Phase 7).
- Hardpoints: `Alamo_Proxy` helper class with hidden ClassDesc for legacy Class_ID compatibility (Phases 7c, 10d).
- Multi-clip animation authoring via Utility-panel "Animation Settings" rollout: add / delete / navigate clips, range scrubbing, file-load + undo/redo refresh (Phases 11b.2, 11b.2.1, 11b.2.2).
- Legacy `.max` clip-data import: opens legacy Petroglyph-authored files and surfaces their clip catalogue automatically (Phase 11c).
- Shadow-volume mesh validation with non-fatal `.export.log` + Listener warnings on open/non-manifold meshes (Phase 12).
- Billboard pivot convention documented; `_ALAMO_BILLBOARDS` legacy MaxScript hook honored as back-compat (Phase 12.1).
- Alamo Utility command-panel rollouts:
  - Node Export Options (Phase 5d).
  - Billboarding Options with 8 modes (Phase 5d).
  - Quick Selection Utility (Phase 5d).
  - Animation Settings (Phase 11b.2).
  - Propagate-visibility-to-descendants helper button (Phase 8f).

### Added — testing & quality

- Catch2 v3 test suite at `alamo_format/tests/`: 151 cases / 827 assertions covering chunk reader/writer, format builders, shader table, skinning, anim-clip-list parsing, legacy clip-scan, shadow-volume topology.
- Max-side harness at `tests/maxscript/test_*.ms`: end-to-end scene-author → export → verify pipeline driven by `3dsmaxbatch.exe`. Currently ~50 harness tests, each paired with a Python verifier.
- Tier 1 invariant validator (`tests/maxscript/verify/validate_alo.py`): strict mode for our walker output + loose mode calibrated against the entire vanilla corpus.
- Corpus baselines:
  - Round-trip: 2,066 / 2,066 vanilla `.alo` byte-identical.
  - Shadow-volume validator: 75.5% of vanilla shadow meshes pass the closed-manifold check (the other 25% is vanilla authoring noise the legacy Petroglyph exporter would have warned on too).
  - Billboard convention: 14 / 14 vanilla `PARALLEL` + `SUNLIGHT_GLOW` meshes verified at exactly `(0, -1, 0)` bone-local-Y face normal.

### Documentation

- [`docs/format-notes.md`](docs/format-notes.md) -- working specification of the Alamo `.alo` / `.ala` chunk formats, updated continuously throughout reverse-engineering work.
- [`docs/build.md`](docs/build.md) -- local build steps, Max 2026 SDK setup, Tier 4 manual smoke-test checklists per major phase.
- [`docs/development-log.md`](docs/development-log.md) -- phase-by-phase project history, decisions, and where-things-live navigation.
- [`docs/release.md`](docs/release.md) -- release procedure (new in this version).
- [`docs/wishlist.md`](docs/wishlist.md) -- pre-scope feature ideas.

### Known limitations

- The `.dle` Max plugin is built locally only; Max SDK is non-redistributable so CI cannot ship it. Each GitHub Release attaches a manually-built `.dle` per `docs/release.md`.
- **Collision-tree chunks (`0x1200`-`0x1203`) writer not yet implemented.** Phase 9.1 partial decode established that vanilla collision meshes carry these chunks 100% of the time; our exports omit them and rely on the engine's load-time runtime fallback (collision works in-game, just not byte-identical to vanilla). Tracked in [`docs/wishlist.md`](docs/wishlist.md) for v1.x.

### Phase 9.1 — format spec close-out (pre-v0.9.0)

- **Resolved `0x402` mesh-metadata bbox layout.** 6 floats at offset `+4` are `(min[3], max[3])` AABB corners. Confirmed against AloViewer source (`src/Assets/Models.cpp:184-190`) and empirically (69/69 vanilla meshes match within `1e-3` tolerance). Our writer already emitted this layout; the TODO-validate comment was removed.
- **Resolved `0x10002` vertex-format chunk payload.** Just the null-terminated format-name string. (Dev-log "Open format questions" section was stale; corrected.)
- **Partially mapped collision tree** (`0x1200`-`0x1203`). Tree shape established: `0x1201` is always 40 bytes (fixed root header), `0x1202` scales ~12 bytes/triangle (variable AABB-tree body), `0x1203` is exactly `faceCount × uint16` (face-index permutation list). Full byte-level decode deferred to v1.x.
- New `scripts/inspect_mesh_bbox.py` empirical inspector committed under `scripts/` for future use.

[Unreleased]: https://github.com/DrKnickers/max2alamo-2026/compare/v0.9.0...HEAD
[0.9.0]: https://github.com/DrKnickers/max2alamo-2026/releases/tag/v0.9.0

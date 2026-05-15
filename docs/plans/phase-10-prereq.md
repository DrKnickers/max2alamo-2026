# Phase 10 prerequisite session — `_ALAMO_VERTEX_TYPE` plumbing

> **Status**: blocked-pending-prereq. Investigation complete (this session, 2026-05-15); two empirical Tier 4 checks needed before PR A lands. See [issue #75](https://github.com/DrKnickers/max2alamo-2026/issues/75) for full plan.

## Why this is its own session

Phase 10 has two risks that source-code reading alone can't resolve:

- **R7** — AloViewer's renderer reads `0x10002` strictly (confirmed from
  [`VertexManager::GetVertexFormat`](https://github.com/GlyphXTools/alo-viewer/blob/master/src/RenderEngine/DirectX9/VertexManager.cpp)).
  Does the shipping EaW/FoC engine bind vertex declarations the same way?
  If yes, AloViewer alone is the verification oracle. If no, we have a
  divergence to characterize.
- **R8** — Vanilla content puts a `0x10006` per-submesh skin-bone-remap
  chunk on `RSkin*` / `B4I4*` skinned submeshes (779 / 10,737 submeshes
  in the corpus, 100% correlation with skinned formats). Our walker
  never emits one. AloViewer's loader treats it as optional. Does the
  engine require it for skinned meshes to render correctly?

Both questions need a live render to answer. The session that resolves
them is short (one round of authoring + export + load) but its outcome
shapes Phase 10's scope:

- **If R8 says `0x10006` is required**: Phase 10 grows to include a skin-
  bone-remap writer + corresponding `ExportSubmesh` field.
- **If R8 says it's optional**: Phase 10 ships only the vertex-format-
  string plumbing.

## Background snapshot

(Lifted from issue #75 investigation; quoted here so this doc is
self-contained.)

- Our writer emits a fixed `0x10002 = "alD3dVertNU2"` for every submesh
  ([`alo_build.cpp:18`](../../alamo_format/src/alo_build.cpp#L18)).
- Vanilla corpus uses 10 distinct vertex-format strings (out of
  AloViewer's 15-name recognized set). Strict 1-to-1 shader↔string
  mapping across 10,737 submeshes — strongly implying the string is
  load-bearing for renderer binding.
- AloViewer source confirms: the loader normalizes `0x10005` /
  `0x10007` bytes into a single 144-byte `MASTER_VERTEX`; the renderer
  then uses the `0x10002` string to pick a GPU vertex declaration. The
  declarations differ in bone-influence count (RSkin reads 1 bone,
  B4I4 reads 4 with weights).
- Our walker fills 4 bone slots with weights (Phase 5c). The matching
  format-name family is therefore `alD3dVertB4I4*`, not `alD3dVertRSkin*`
  (which would silently discard slots 1..3).

## Experiment design

### Goal

Two binary outcomes:

1. **R7** — given a known-good vanilla `.alo` with a known shader+format
   pair, hex-edit *only* the `0x10002` string to a different valid name
   from the AloViewer table, then load in the real EaW/FoC engine.
   Does the engine accept the modified file? Does it render correctly?
   Is there a way to distinguish "renders identical" from "renders
   subtly wrong"?

2. **R8** — take a vanilla `.alo` with a `0x10006` skin-bone-remap chunk
   (e.g., `RSkinBumpColorize.fx` submesh — 217 in the corpus). Strip
   the `0x10006` chunk. Load in AloViewer and in EaW/FoC. Does the
   skinning still work? Are bone influences mapped correctly?

### Fixtures

The 5 variants below have been pre-built at
`tests/fixtures/phase-10-prereq/` (gitignored — derived from
`tests/corpus/eaw/EI_NAVYTROOPER.ALO`, which is Lucasfilm IP). Each
differs from `01_control.ALO` by exactly one chunk-level change:

| File | `0x10002` rewrite (skinned submeshes only) | `0x10006` skin-remap chunks | Tests |
|---|---|---|---|
| `EI_NAVYTROOPER_01_control.ALO` | original `alD3dVertB4I4NU2` | present (2) | reference |
| `EI_NAVYTROOPER_02_vfmt_rskin.ALO` | rewritten → `alD3dVertRSkinNU2` | present | R7: RSkin vs B4I4 family swap. RSkin reads 1 bone via packed `Normal.w`; B4I4 reads 4 bones via `BlendWeights[4]+BlendIndices`. Per vertex-data the file is already B4I4-style (4 slots populated), so the RSkin renderer will use only slot 0 — expect *visibly less smooth* skinning at joint seams. |
| `EI_NAVYTROOPER_03_vfmt_basic_static.ALO` | rewritten → `alD3dVertNU2` | present | R7: tag skinned data as static. Vertex shader skips skinning entirely. Expect mesh to render at bind-pose positions (probably *collapsed to origin* or whatever the un-transformed vertex positions yield). |
| `EI_NAVYTROOPER_04_no_skin_remap.ALO` | original | **stripped** | R8: does the engine require `0x10006`? If skinning works without it, our walker omitting it (as it does today) is safe. If broken — wrong bones referenced — we need a `0x10006` writer in Phase 10. |
| `EI_NAVYTROOPER_05_vfmt_rskin_no_remap.ALO` | rewritten → `alD3dVertRSkinNU2` | **stripped** | R7+R8 combined sanity. |

The 4 static submeshes in NAVYTROOPER (`alD3dVertNU2`, `alD3dVertN`)
are left untouched in every variant — only the 2 skinned submeshes
get rewritten. This keeps the rest of the model rendering correctly
and isolates the variable being tested.

All 5 variants round-trip cleanly through `alo_roundtrip.exe`, so
they're well-formed chunks even after the hex edit.

The fixture builder is `scripts/hex_edit_alo_chunks.py` —
`--rewrite-vfmt --only-current <str>` filters which chunks to rewrite;
`--strip-skin-remap` deletes 0x10006 chunks. To rebuild from a
different control source:

```
python scripts/hex_edit_alo_chunks.py --rewrite-vfmt <in.alo> <new_name> <out.alo> [--only-current <existing_str>]
python scripts/hex_edit_alo_chunks.py --strip-skin-remap <in.alo> <out.alo>
```

### Procedure

1. Open `EI_NAVYTROOPER_01_control.ALO` in AloViewer first. Note the
   reference rendering — pose, skin smoothness, any visible deformation.
   Take a screenshot.
2. Open each of the remaining 4 variants. For each, observe:
   - Does it load without error?
   - Does the mesh render at all?
   - Skin deformation — same as control / degenerate (collapsed
     vertices) / statically posed (no skinning) / something else?
   - Any console / dialog warnings?
3. For variants that load in AloViewer, drop each into a real EaW or
   FoC mod-tools build that loads at game-start. Same observations.
4. Fill in the "Results" section below.

### Capture format for results

For each variant, fill in:

```
### <variant>
- AloViewer load: ok / error: <text>
- AloViewer render: matches-control / mesh-collapsed / mesh-static / other: <text>
- Engine load: ok / error / not-tested
- Engine render: matches-control / wrong-skinning / other / not-tested
- Screenshot: <path-or-link>
- Notes: <text>
```

### Decision matrix

| R7 outcome | R8 outcome | Phase 10 scope |
|---|---|---|
| Engine renders correctly with any valid `0x10002` and no `0x10006` | Skinning works without `0x10006` | Ship as planned (writer string + walker fields + table) |
| Engine renders correctly with valid `0x10002`, but skinning needs `0x10006` | Skinning breaks without it | Add a `0x10006` writer; minimal — emit `{0,1,2,...,N-1}` covering all referenced bones |
| Engine diverges from AloViewer on string interpretation | — | Characterize the divergence; may need engine-specific table or fall back to a vanilla-conservative mapping |
| Engine refuses to load when `0x10002` is rewritten | — | Suggests strict signature check — investigate format-version coupling |

The third and fourth rows are unlikely (vanilla shipping content
contains 10 distinct strings, so the engine clearly handles them all)
but worth naming.

## Out of scope for this session

- Writing PR-A code. The whole point of the prereq is to land the right
  PR-A, not jump ahead.
- Authoring fresh content from Max — the experiment uses hex-edited
  vanilla files so behavior differences are attributable to the *only*
  bytes that changed.
- Custom-`.fx`-shader workflow. That's PR B/C territory.

## Estimated time

- Fixture build (Claude, pre-session): 30 min.
- Variant rendering + observation (you): 30 min × 6 variants = 3 hours
  at most.
- Result writeup back into this doc: 15 min.

Total: half a day if everything cooperates. The blocker is having Max +
AloViewer + an EaW or FoC install in front of you.

## After the session

Update this doc's "Results" section, then move on to PR A from issue #75
with whatever scope the results dictate.

## Results

*To be filled in after the prereq session runs.*

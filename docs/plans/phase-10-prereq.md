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

### Fixture preparation (Claude can do this for you ahead of the session)

A small Python helper `scripts/hex_edit_alo_chunks.py` (write this on
the day) that supports two operations:

- `--rewrite-vfmt <input.alo> <new_name> <output.alo>` — finds every
  `0x10002` leaf chunk and rewrites its cstring payload to `<new_name>`.
  Pads with nulls to keep payload size constant (so downstream offsets
  don't shift) — guarded by a check that `len(new_name) + 1 <=
  original_chunk_size`.
- `--strip-skin-remap <input.alo> <output.alo>` — finds every `0x10006`
  leaf chunk inside `0x10000` submesh-data containers and removes it,
  rewriting parent container sizes accordingly.

Pick a small candidate file with both properties (skinned + has 0x10006).
Suggested from the corpus survey:

- **For R7**: any small file containing exactly one `RSkinBumpColorize.fx`
  submesh. Survey shows 217 candidates; pick one with vertex count
  < 5000 to keep validation eyeball-friendly.
- **For R8**: same file works.

Variants to produce:

| Variant | `0x10002` string | `0x10006` chunk | Expected AloViewer behavior |
|---|---|---|---|
| `control` | original | original | renders correctly |
| `vfmt_b4i4` | `alD3dVertB4I4NU2U3U3` | original | renders correctly (4-bone path with weights) |
| `vfmt_rskin_swap` | swap RSkin↔B4I4 | original | may render differently — characterize |
| `vfmt_wrong` | `alD3dVertNU2` | original | wrong layout — should visibly break |
| `nostrip_remap` | original | absent | does AloViewer / engine cope? |
| `vfmt_b4i4_no_remap` | `alD3dVertB4I4NU2U3U3` | absent | does B4I4 path need 0x10006? |

### Procedure

1. Pre-build the 6 variants on disk before opening any GUI. Each variant
   should byte-diff from `control` *only* in the targeted chunk.
2. Open each in AloViewer. Record observations:
   - Does it load without error?
   - Does the mesh render at all?
   - Skin deformation — visually correct vs. degenerate (collapsed
     vertices) vs. statically posed (no skinning)?
   - Any console / dialog warnings?
3. For the variants that load in AloViewer, drop them into a real
   EaW or FoC mod-tools build that loads at game-start. Same
   observations.
4. Tabulate results in this doc under "Results".

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

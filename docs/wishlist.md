# Wishlist

> Lightweight running list of features and improvements that are wanted but not yet scoped into a phase. Use this when you have an idea worth capturing but don't want to lose it in conversation; promote items into the dev log's "Future phase plans" section once they're scoped, or into a [GitHub issue](https://github.com/DrKnickers/max2alamo-2026/issues) once they're ready to start.
>
> Each item should be enough for a future session to pick up cold — what the feature does, why we want it, the rough shape of an implementation, and the open questions.
>
> Once an item ships, move its entry into [`docs/development-log.md`](development-log.md) and delete from here.

---

## 1. Selective export — hardpoint sub-models

**Status:** idea (not scoped, no PR yet).

**Use case.** Alamo unit assets often consist of a main hull `.alo` plus several hardpoint sub-`.alo` files (turrets, weapons, attached props). Hardpoint sub-models are referenced from the parent unit's `.alo` by bone name. Currently, our exporter writes the entire visible scene as one `.alo` per File → Export invocation. Authoring a hardpoint workflow today means juggling separate `.max` files per sub-model, with the parent-rig bones duplicated across files for naming-consistency reasons.

The wished-for workflow: keep the whole unit (hull + every hardpoint) in one `.max` scene, and let the exporter slice out groups on demand into separate `.alo` outputs.

**Sketch.** Two natural authoring affordances, not mutually exclusive:

1. **Group user-prop** on meshes / bones / helpers — e.g. `Alamo_Export_Group = "HP_R_TRB01"`. Nodes without the prop go to the main `.alo`; nodes with the prop go into a sibling `<basename>_HP_R_TRB01.alo`. Multiple groups → multiple sibling `.alo` files in a single export pass. Mirrors the Phase 11b multi-clip animation precedent (pipe-delimited string on rootNode lists the groups; the exporter loops).
2. **Selection-based "Export Selected as Hardpoint"** menu entry — leverages Max's existing selection. Cheaper to author for one-off sub-models, but harder to round-trip (a `.max` file doesn't persist "the most recent selection").

(1) is the better authoring model for production hardpoint authoring (a single source-of-truth scene); (2) is a useful escape hatch and might be cheaper to ship first.

**Open questions / decisions to make before scoping.**

- **Bone hierarchy in the sub-model.** A hardpoint sub-`.alo` typically wants its own minimal skeleton (one Root bone + the bones the hardpoint's meshes/lights actually reference). If the modder authors with the full unit skeleton in the scene and selects only the hardpoint subtree, do we (a) emit only the referenced bones and re-parent them under a synthetic Root, (b) emit the full bone chain back up to Root for naming-consistency with the parent unit, or (c) make this a per-group setting?
- **Animations.** The parent unit owns the animation rig; hardpoint sub-models might have their own per-clip animations (the turret has separate "fire / recoil / idle" clips not present on the parent). Does the group selection scope the `Alamo_Anim_Clips` filter? Or is animation scope independent of geometry scope?
- **Helpers and lights.** Hardpoints typically have their own muzzle-flash lights and exhaust dummies. The group prop on a helper or light needs to route the same way as on a mesh.
- **Modes of operation.** Export-all-groups-at-once on a single File → Export click, or one-group-per-export with a group-picker dialog? Phase 11b precedent (multi-clip emits N sibling `.ala` files in one export) suggests the former.
- **Mike Lankamp's importer round-trip.** If we emit sibling `<basename>_<group>.alo` files, Mike's importer can already discover them by filename glob (same pattern as Phase 11b's `_<clip>.ala` siblings). Re-importing the parent would NOT pull in the hardpoint sub-models automatically — they'd need to be opened separately. Acceptable per existing convention.
- **Existing user-prop family overlap.** We already use `Alamo_Export_Transform`, `Alamo_Export_Geometry`, `Alamo_Hidden`, etc. A new `Alamo_Export_Group` joins that family naturally; pick the name to match existing convention.
- **What does "all the rest goes into the main .alo" mean** when the user wants ONLY the hardpoints, no hull? Either a sentinel value (`Alamo_Export_Group = "*main*"`) authored on the hull subtree, or a special "hardpoints-only export" mode, or just rely on the user to set `Alamo_Export_Transform=false` on the hull when they want hardpoints-only.

**Existing code to lean on.**

- [`max2alamo/src/alamo_utility.cpp`](../max2alamo/src/alamo_utility.cpp) already has the Utility-panel infrastructure for per-node user-prop editing.
- [`max2alamo/src/scene_walker.cpp`](../max2alamo/src/scene_walker.cpp) walks the scene producing one `ExportScene`. The straightforward implementation is to make `walk_scene` return `std::vector<NamedExportScene>` and have the exporter loop, mirroring Phase 11b.1's multi-clip walker change.
- [`max2alamo/src/alo_export.cpp`](../max2alamo/src/alo_export.cpp) writes one `.alo` per export today; per-group multi-emission is a fairly mechanical extension. `.export.log` already supports multi-line per-output summaries from Phase 11b.1.

**Likely effort.** 1–2 sessions. Phase 11b.1 (multi-clip) is the closest precedent both in walker-change shape and in test scaffolding; an analogous "split scene into N export groups" change in `walk_scene` plus per-group `.alo` emission in `alo_export.cpp` would carry most of the weight.

---

<!-- New items: append below this line. Keep entries cheap to drop in -- no rigid format required. A use case + a sketch + the open questions is enough. -->

---

## 2. Collision-tree writer for `0x1200` chunks — ✅ SHIPPED in Phase 9.2

(This entry is kept briefly for history; the implementation lives at `alamo_format/include/alamo_format/collision_tree.h` + `.cpp`, the spec is documented in `docs/format-notes.md` "Collision tree (`0x1200`, Phase 9.2)", and the writer is wired into `alo_build.cpp::build_submesh_geometry` for any `ExportMesh` with `is_collision = true`. Will be deleted from this file in a future cleanup once it's clear the entry isn't needed for ongoing context.)

**Original status:** partially scoped (format-level investigation done Phase 9.1; full byte-level decode pending). Not blocking v0.9.0 release.

**Use case.** Every vanilla EaW + FoC collision mesh ships with a `0x1200` collision-tree container (142/142 in a 100-file FoC sample). Our exporter currently omits it; modder-shipped exports still collide in-game (engine builds a fallback at load time), but bypassing the runtime build would shave a tiny load-time cost and, more importantly, would put our output structurally identical to vanilla content — which matters for tools like Mike Lankamp's `alamo2max.ms` round-trip and AloViewer's "view as authored" expectation.

**Sketch (per `docs/format-notes.md` Q5 partial decode):**

- `0x1201` (always 40 bytes): root-node header. Believed to contain a world-space AABB (4 floats observed near `+2`..`+14`) plus per-axis counts at the tail (`+26`..`+40` has byte-level fields that look like `(type, ?, uint16_count, 0)` pairs). Full layout requires more empirical samples or Ghidra.
- `0x1202` (variable, ~12 bytes per triangle): internal AABB-tree node body. Quantized 8-bit AABB fields per node (observed pattern `00 00 00 ff ff ff` for axis-aligned per-byte min/max in a normalized cell space). Probably encodes `(quantized_bbox, child_index, leaf_index)` per record.
- `0x1203` (exactly `faceCount × uint16`): face-index permutation list — the order in which faces are referenced by the tree's leaves. Confirmed scaling across 4 samples (330/252/374/12 triangles → 660/504/748/24 bytes).

**Open questions / decisions to make before scoping:**

- **Tree type and split heuristic.** Looks like an AABB tree (likely a BVH). Need to confirm whether splits are surface-area-heuristic (SAH) or median-axis or something cheaper. Engine probably doesn't care which heuristic produced it as long as the tree shape parses; we can pick a simple one.
- **Full `0x1201` and `0x1202` decode.** The 40 bytes of `0x1201` and the per-record fields of `0x1202` aren't byte-level decoded yet. Two paths:
  - Ghidra against EaW's `gameobject.dll` — find the chunk-load callsite, walk back to the consumer. Most definitive; significant time investment.
  - More empirical-diffing: take two collision meshes with KNOWN differences (one triangle vs many triangles; long-thin vs cube-shaped) and diff their `0x1201` / `0x1202` payloads byte-by-byte to isolate which fields are size-dependent vs structure-dependent.
- **Whether to write it at all** vs. continue to lean on the engine's runtime fallback. If the runtime fallback is meaningfully slower or produces worse trees, we should write; if it's cheap and equivalent, we can skip (and document that our exports are intentionally `0x1200`-less).

**Existing code to lean on.**

- [`alamo_format/src/alo_build.cpp`](../alamo_format/src/alo_build.cpp) — builds the chunk tree per mesh; adding `build_collision_tree(...)` and emitting it under `0x10000` is the natural seam.
- `0x1203`'s structure is already fully understood (one uint16 per face); the hard part is the tree-node serialization in `0x1202`.

**Likely effort.** 2–4 sessions if Ghidra is required; 1 session if empirical-diffing + tree-shape inference is enough. Defer until v1.0 ships or a modder requests it.

---

## 3. `.alo` importer (re-import an exported `.alo` back into Max)

**Status:** post-v1 idea per the README roadmap.

The format library's read side is already in place (exercised by `alo_dump` + the round-trip oracle in `alo_roundtrip`); only the Max-side glue remains. Modern Max 2026 replacement for Mike Lankamp's `alamo2max.dlu` Max 9 plugin. Not scoped further yet — would be its own project once the exporter is at v1.0 stability.

<!-- Append future items above this comment. -->


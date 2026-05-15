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

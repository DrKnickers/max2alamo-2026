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
| 8c | Walker translation tracks: extends `walk_animation()` to scan per-frame `Matrix3::GetRow(3)` for every animatable bone, compute per-bone `trans_offset` (min) / `trans_scale` (max-min)/65535, pack `uint16[3]` into `0x100a`. Single `GetLocalTM` per (bone,frame) shared with rotation extraction. Scale tracks confirmed absent from FoC vanilla (0/1500); 8c emits `n_scale_words=0` and leaves `idx_scale=-1`. | ✅ shipped | `test_translation_keyframes` (2-bone scene: TransBone keyframed [0,0,0]→[10,20,5], RotBone constant position + 0°→90° Z rotation). 25-assertion verifier covers structural, slot assignment, rotation, and translation groups; all pass. Frame[0] position ≈ (0,0,0), frame[30] ≈ (10,20,5) within 1e-3. RotBone has degenerate `trans_scale=(0,0,0)` (constant position) — exercises the divide-by-zero guard. 26/26 harness; 3× byte-identical determinism (`.alo` `A0C333A2…`, `.ala` `749EBCE8…`); Phase 7d test still SHA `56FCEAF0…` post-rebuild. Non-regression: 4929/4929 · 2066/2066 · 2863/2863 · 81/81. Three negative tripwires fire (dropped keyframe → constant pos; swapped scale axes → per-axis values wrong; off-by-3 word count → pool-size mismatch). |
| 8e | Full animation-surface integration test: multi-feature scene (11 bones / 2 meshes / 2 lights / 2 proxies) overlaying rotation (8b), translation (8c), visibility (8d), and pivot-orientation (5g) on the Phase 7d static-surface shape. 4 animated bones (BoneA / BoneB / HelperBone / PivotedHardpoint) exercise every category in `bone_map`; 3 visibility-animated nodes (OmniLight / StaticBox / prox0) exercise the broader `visibility_map` surface. PivotedHardpoint tests 5g × 8b cross (static `objectoffsetrot` composed with per-frame node rotation). SpotLight / SpotLight.Target / prox1 are control bones with no animation — the no-spurious-tracks gate proving the bone_map / visibility_map split is doing its job. **No walker changes** — 8e is pure test addition. | ✅ shipped | `test_phase8_acceptance`: 62-assertion verifier across 10 groups (A structural / B skeleton / C pool size + bone_map enforcement / D rotation incl. 5g×8b / E sign canonicalisation / F translation / G visibility / H skinning unaffected by animation / I chunk ordering / J `.export.log` cross-check) — all pass. 11 bones, 16 rotation words, 12 translation words, 0 scale words, 3 visibility tracks. **31/31 harness.** Hard merge gate: **all 10 prior gold SHAs preserved post-rebuild** (7d, 8b .alo, 8b .ala post-8c stable `19a79cbb…`, 8c .alo+.ala, 5g×3, 8d .alo+.ala). 3× byte-identical determinism (`.alo` `dbc4f82b…`, `.ala` `99a74727…`). Non-regression: 4929/4929 chunk-tree · 2066/2066 .alo strict · 2863/2863 .ala typed-pipeline · 81/81 unit. **Five negative tripwires** all fire expected assertions: (A) broaden `bone_map` post-walks → #C14/#C15/#C21 fire (rot_words 40 ≠ 16; light/proxy bones get unwanted slots); (B) drop `visibility_map[inode]` in walk_proxies → #G45 fires (prox0 no 0x1007); (C) swap `bone_map` for `visibility_map` in third pass → #G39+#G42+#G45 all fire (all 3 visibility tracks lost); (D) drop `compose_with_object_offset` in per-frame loop → #D27+#D28 fire (PivotedHardpoint composition broken); (E) invert visibility threshold `>= 0.5` → `<` → 9 visibility assertions fire (frame bits flipped + spurious 0x1007 on previously-elided bones). |
| 8d | Walker visibility tracks (`0x1007`): introduces a second `visibility_map` (`INode* -> bone index`) distinct from `bone_map` -- populated by `walk_bones` (real + helpers-as-bones), `walk_lights` (light + `.Target`), `walk_proxies`, and `walk_node` (static-mesh attachment branch only). `walk_animation` gains a third pass that samples `INode::GetVisibility(t, nullptr)` per frame, thresholds at `>= 0.5`, and appends a `0x1007` leaf to `out_anim.bones[bone_idx].track_leaves` only when at least one frame is hidden (constant-visible elision matches vanilla). New `pack_visibility_bits()` helper packs LSB-first per byte, `ceil(n_frames/8)` bytes total. **Zero format-library changes** -- Phase 8a's `AlaBoneTrack::track_leaves` carries the chunk verbatim. | ✅ shipped | `test_visibility_blinking_light` (Omni light keyframed `at time 0 true` / `at time 30 false`, plus `AlwaysVisible` BoneSys bone with no animation + `AnchorBox` mesh). **15-assertion verifier** across 3 groups (structural / chunk emission / bit-packing semantics + monotonicity), all pass. BlinkingLight 0x1007 payload `[FF 03 00 00]` = frames 0..9 visible, 10..30 hidden (Max's on_off controller bezier-interpolates to a transition around frame 9/10). AlwaysVisible is elided per the constant-visible rule. 30/30 harness; 3× byte-identical determinism (`.alo` `f2f027ff…`, `.ala` `7fe1478a…`). **Hard merge gate:** Phase 7d (`56FCEAF0…`), 8c .alo (`A0C333A2…`) + .ala (`749EBCE8…`), and three 5g pivot tests all unchanged post-rebuild — proving 8d is a true no-op for scenes without animated visibility. (Plan's stale `test_rotation_keyframes.ala` SHA `2B60515B` is an 8b-era value invalidated by 8c's translation-pool emission to all animated bones; current stable SHA `19a79cbb…` reproduces byte-identically.) Non-regression: 4929/4929 · 2066/2066 · 2863/2863 · 81/81. Three negative tripwires (remove visibility keys → #B7; flip LSB→MSB in pack_visibility_bits → #C14 monotonicity; invert bit semantics → #C12 + #C13 + #C14). |
| 5g | Pivot-orientation fix (closes [#53](https://github.com/DrKnickers/max2alamo-2026/issues/53)): walker now composes `objectoffsetrot` / `objectoffsetpos` from `INode::GetObjOffset*()` into every on-disk bone matrix and every per-frame animation sample (7 callsites). New `compose_with_object_offset(INode*, Matrix3)` helper does `offset_TM * node_TM` (row-vector composition; offset applied first). Captures the "Affect Pivot Only" hardpoint authoring workflow for Dummy/Point helpers. | ✅ shipped | `test_pivot_node_rotation`, `test_pivot_affect_only`, `test_pivot_skinned_safety` — three new harness tests promoted from one-shot probes. DummyPivoted col1 ≈ (-1,0,0) within 1e-3 (the bug, now captured). 29/29 harness; gold SHAs preserved on Phase 7d (`56FCEAF0…`), 8b (`3791F274…`), 8c (`A0C333A2…`) post-rebuild — fix is a true no-op for scenes without object offsets. Non-regression: 4929/4929 · 2066/2066 · 2863/2863 · 81/81. Caveat: BoneSys bones double-apply objectoffsetrot via Max-internal logic; users should rotate BoneSys bones via `node.rotation` or the interactive Rotate tool. BoneSys `createBone <from> <to>` direction args are NOT recoverable (data is in bone-object internals; no Max-SDK accessor). |
| Utility UI | Faithful clone of the legacy PG Alamo Utility panel | ✅ shipped | Three rollouts (Node Export Options / Quick Selection / Animation Settings) appear under Utilities > More... > Alamo Utility; checkboxes/radios round-trip Alamo_* user properties on the selected node |
| 6a | Effects11 shader stubs for Max 2026 | ✅ shipped | [PR #18](https://github.com/DrKnickers/max2alamo-2026/pull/18) — all 39 PG shaders load in Max 2026's DXSM with PG parameter UIs |
| 6b | Per-vertex tangent + binormal export | ✅ shipped | [PR #19](https://github.com/DrKnickers/max2alamo-2026/pull/19) — MikkT via `IGameMesh::GetFaceVertexTangentBinormal`; bump shading works |
| 6c | Per-material parameter export | ✅ shipped | [PR #20](https://github.com/DrKnickers/max2alamo-2026/pull/20) + [#22](https://github.com/DrKnickers/max2alamo-2026/pull/22) — DXMaterial ParamBlock → typed `0x10103/6` chunks; float3 alpha=0 convention |
| 6d | Coord-frame banner in `.export.log` | ✅ shipped | [PR #21](https://github.com/DrKnickers/max2alamo-2026/pull/21) — every export documents `Z up, -Y forward, +X right` |
| Test harness | Max-side regression suite | ✅ shipped | [PR #23](https://github.com/DrKnickers/max2alamo-2026/pull/23) + [#26](https://github.com/DrKnickers/max2alamo-2026/pull/26) — `scripts/run-max-tests.ps1` runs 6 end-to-end tests via `3dsmaxbatch` |
| 7 | Lights, hardpoints, proxies | ✅ shipped | Format library + walker + helper plugin class all in place; 24/24 harness covers Omni/Directional/Spotlight/`.Target`/proxies/flags + the 7d full-scene integration. In-game fighter Tier 4 deferred to Phase 8/9. |
| 8 | Animation export incl. visibility tracks | ✅ shipped | Walk cycle + animated visibility play correctly in-game. **Sub-phases:** 8a typed `.ala` read/write pipeline ✅; 8b walker rotation pass ✅; 8c walker translation tracks ✅ (scale tracks confirmed absent from FoC vanilla, skipped); 8d visibility tracks ✅; 8e full integration acceptance ✅; 8f regression battery + visibility-propagation Utility-panel button ✅. |
| 10d | Hidden ClassDesc registration for the LEGACY Petroglyph max2alamo.dle's helper class -- `Class_ID(0x52263841, 0x08194485)` SCID `HELPER_CLASS_ID` (0x50), identified via Phase 10a's `.max` OLE-storage parser (description string in the class table: "Exporter and tools for the Alamo engine"). Second `ClassDesc` (hidden, `IsPublic()=FALSE`) instantiates the same `AlamoProxyHelper` class. Because `AlamoProxyHelper::ClassID()` always returns the MODERN ID `kAlamoProxyClassID`, files opened via the legacy ClassDesc **auto-migrate** to the modern ID on first save -- one open+save cycle upgrades the file, subsequent opens find the modern ID directly. **Bonus tale**: my first attempt registered the wrong Class_ID (`Class_ID(0x23b708db, 0x00000100)` SCID `0x5ddb3626`) because Phase 10a's `.max` chunk parser had a size-interpretation bug -- it read `chunk_size & 0x7FFFFFFF` as body-only, but Max's `.max` chunk format counts size INCLUDING the 6-byte header. The parser was finding only 1 of 52+ classes per file and the one it found was `CustAttribContainer` (a stock Max class), which my buggy parser mis-labeled as a helper. Fixed the parser, re-ran, found 56 entries in MC80_D's class table including the actual "Alamo Proxy" entry for the legacy plugin. | ✅ shipped | The legacy ClassDesc resolves the 62 + 3 `Missing_Helper` substitutions in MC80_D + IFTX_D into real `Alamo_Proxy` instances. Audit re-run shows node-type breakdown shifts from `62: Missing_Helper` to `62: Alamo_Proxy` for MC80_D. MC80_D `.alo` file size delta `+2202 bytes` confirms the 62 nodes now emit as `0x603` proxy chunks (~35 bytes/proxy × 62) rather than just helper-as-bones. MC80_D regression verifier strengthened with #A4 to assert proxy count >= 50 (catches a future regression where the ClassDesc registration breaks). All 4 legacy regression tests pass (`test_legacy_cis_sbd` 25/4, `test_legacy_snowtrooper` 36/6, `test_legacy_mc80_d` 78/12 + 62 proxies, `test_legacy_iftx_d` 6/2 + 3 proxies). 47/47 full harness; 11 prior gold SHAs preserved. Non-regression on corpus (4929/2066/2863 vanilla + 3 extra legacy `.alo` files riding along in `tests/corpus/legacy/`). Parser bug-fix lives in `re/scripts/dump_max_class_table.py` so the diagnostic tool stays useful for any future legacy-format research. |
| 10b/c | Legacy max2alamo.dle (.max from Max 9 era) compatibility: walker fix to stop emitting spurious 1-frame `.ala` siblings for static-only scenes (Phase 8f's single-frame fix had a regression where `read_node_user_prop_int(... 0)` returned 0 for absent props, looking the same as explicit `0,0` clip), plus 4 new regression tests using Thrawn's Revenge: Ascendancy as ground-truth fixtures (CIS_SBD infantry, EI_SNOWTROOPER infantry, RV_MonCalCruiser_D capship destroyed-state, EV_IFTX_D vehicle destroyed-state). Phase 10a audit confirmed all 5 ThrREv fixtures load cleanly in Max 2026 from the ~17-year-old Max 9 file format AND export valid `.alo` through our plugin — conscious convention-matching across Phases 4-8 (`Alamo_*` user props, `Alamo_Proxy` Class_ID, 39 PG shader stubs, IGame walking) means legacy content basically just works. Walker fix is 1-line (sentinel default `-1` for `Alamo_Anim_Start/End`, gates the .ala-emission path on explicit clip metadata). Harness extended: `test_legacy_*` runs `validate_alo.py` in `--loose` mode (legacy artist content has degenerate-tangent corner cases like `tangent ~ normal` at UV seams that strict mode rejects — same setting `sweep_corpus_validator.py` uses for the vanilla EaW/FoC corpus). | ✅ shipped | 4 new regression tests pass: `test_legacy_cis_sbd` (25 bones / 4 meshes), `test_legacy_snowtrooper` (36 bones / 6 meshes), `test_legacy_mc80_d` (78 bones / 12 damage chunks; the 62 Missing_Helper substitutions emit as helper-as-bones via `Alamo_Export_Transform` user-prop fallback — Phase 10d will register a hidden ClassDesc for the legacy plugin's Class_ID `[0x23b708db, 0x00000100]` SCID `0x5ddb3626` so they emit as real `Alamo_Proxy` instances), `test_legacy_iftx_d` (6 bones / 2 damage chunks). Walker fix verified non-regression: Phase 8f's `test_phase8_anim_single_frame` (explicit 0/0 clip) still passes; full harness 47/47 (was 43/43); all 11 prior gold SHAs preserved. Audit script + diagnostic tools live under `re/scripts/` for future investigations (`audit_legacy_max.ms` does a structured scene walk + export attempt, `dump_max_class_table.py` parses .max OLE compound storage to extract plugin Class_IDs from `ClassDirectory3` stream). |
| 8f | Comprehensive regression battery (12 new tests) + visibility-propagation Utility-panel button. Battery covers: biped+skin (real-world rigging), 32-bone skinning stress, mesh hierarchy with corpus-evidence local-only visibility assertion, high vertex count, UV distortion under extreme tile/negative/mirror, UTF-8 name round-trip with Chinese/Japanese/Cyrillic + 100-char Latin, single-frame + 300-frame + 60fps animation extremes, objectoffsetscale silent-drop pinning, no-material + multi-material geometry edge cases. Plus the Alamo Utility panel's "Propagate visibility to descendants" button (`max2alamo/src/alamo_utility.cpp` + dialog template) solving the death-clone authoring pain via tool-side propagation — copies selected node's `isHidden` + visibility controller to all descendants in one click, wrapped in `undo on`. Two walker bugs surfaced + fixed in-PR: (1) static-mesh synth bone now honors `IsNodeHidden()` (was hardcoded `visible=true`, asymmetric vs walk_lights/walk_proxies); (2) `walk_animation` accepts `end == start` single-frame clips (was rejecting valid one-pose animations). | ✅ shipped | 12 new harness tests all pass: biped_animated (5/5; 35 biped bones), skinning_stress (32 bones / 1584 verts), mesh_hierarchy (8/8; pins LOCAL-ONLY visibility against corpus survey of 2066 files showing 0 inheritance evidence + Mike's importer + Gaukler agree), name_round_trip (7/7 UTF-8 byte-identical), anim_single_frame (3/3) + anim_long (4/4 frames 0..300) + anim_60fps (2/2 fps round-trip), object_offset_scale (5g silent-drop pinned), uv_distortion (no NaN/Inf), high_vertex_mesh (43k verts), geometry_edge_cases (no-mat + multi-mat), visibility_propagation (7/7; button copies controllers byte-identical to descendants, scope limited to subtree). **All 11 prior gold SHAs preserved post-rebuild** including 8e (`dbc4f82b…` / `99a74727…`) — confirms the walker bug fixes are no-ops on existing tests. Phase 8 fully closed. |
| 11a | Phase 11 (multi-clip animation + legacy clip-data reader) reference investigation + legacy `max2alamo.dle` ClassDesc inventory. **Research-only, no code/PR.** Surveyed Mike Lankamp's `alamo2max.ms` animation handling (auto-discovers `<basename>_*.ALA` siblings via `GetFiles`, stacks clips sequentially on Max's timeline via internal `AlamoImporter.Helper.AddAnimation name index base end`), Gaukler's Blender plugin (single-file at a time, no auto-discovery, action name = filename), and the vanilla EaW/FoC corpus (1500 + 1363 `.ala` files, one-per-clip with `<UNIT>_<CLIP>.ALA` filename pattern; infantry catalogues 20–56 clips per unit, `EI_TROOPER` peaks at 56). All three sources agree: one `.ala` per clip, sibling of `<UNIT>.ALO`, **format library is Phase 8a-ready** for multi-clip (`AlaAnimation::name` already round-trips). Plus the breakthrough: built `re/scripts/{dump_legacy_classids.py, find_classid_methods.py, resolve_classdesc_via_rtti.py}` (gitignored diagnostic tooling) to walk MSVC RTTI in the legacy `max2alamo.dle` and bind each `ClassDesc` name to its `ClassID()` / `SuperClassID()` returns by reading immediate-value operands from `.text`. (Class_IDs are NOT stored as contiguous 8-byte literals in `.rdata` — they're emitted as two separate `mov [eax+N], imm32` instructions inside `ClassID()` bodies. Byte-grepping the DLL for the Class_ID returns 0 hits; the RTTI walker decodes the immediates.) Methodology validated by recovering the known `Alamo_Proxy` Class_ID(0x52263841, 0x08194485) matching Phase 10d's `.max`-derived value exactly. Resolved 4 ClassDescs in UaW Max 9 binary: `AlamoProxyClassDesc` (SCID 0x50, already handled by Phase 10d), `AlamoDazzleClassDesc` (SCID 0x50, Class_ID(0x0b187920, 0x47e32d75), **NEW** — UaW-Max-9-only addition for *Universe at War: Earth Assault*, 2007; not in EaW/FoC), **`AlamoUtilityClassDesc` (SCID 0x1020, Class_ID(0x70a24090, 0x60c90f03))** — the one Phase 11c needs — and `Max2alamoClassDesc` (SCID 0xa20, Class_ID(0x31f5ae34, 0x7e2d36aa), the legacy `.alo` exporter; not relevant — we have our own). Cross-version verification: instruction-pattern scan on all 4 plugin variants (EaW Max 6, EaW Max 8, FoC Max 8, UaW Max 9; older 3 are `/GR-` RTTI-stripped) confirms Proxy/Utility/Exporter Class_IDs are **byte-identical across all versions** (2006–2007 lifetime), so a single Phase 11c hidden-ClassDesc registration routes legacy data from all three games. Validated against real fixtures: the Utility Class_ID byte pattern appears **60 times at exactly 342-byte stride** in `EI_SNOWTROOPER.max` and `Stormtrooper.max`, and 8 times in `CIS_SBD.max` — confirms the empirical "~342-byte fixed-size record" finding from SESSION_NOTES, gives us the clip-record stride, and decodes the record header layout: `+0` Class_ID, `+8` SCID marker, `+12` uint32 index, `+16/+20/+26` constant sub-chunk sizes (0x118/0x130/0x126), `+34` null-terminated ASCII clip name (first 3 records of EI_SNOWTROOPER = "ATTACKFLINCHB_00" / "ATTACKFLINCHF_00" / "ATTACKFLINCHF_01" matching vanilla EI_TROOPER catalogue), `+50..+342` per-clip data including start/end frames (offsets TBD in Phase 11c). | ✅ shipped (research-only) | Findings docs at `re/output/phase11_research/11a_findings.md` + `re/output/phase11_research/11a_legacy_classid_breakthrough.md` (gitignored). Phase 11b architecture spec locked with user-confirmed decisions: `Alamo_Anim_Clips` user prop (pipe `|`-delimited) + per-clip `Alamo_Anim_<NAME>_Start/_End` on rootNode; lowercase `<basename>_<clipname>.ala` filenames matching Mike's importer glob; partial-export policy emits-others-and-logs (don't abort); back-compat retains un-suffixed single-clip path. Phase 11c was previously blocked on locating the legacy Utility plugin's Class_ID — **now unblocked**: Strategy 2 (hidden ClassDesc with `Class_ID(0x70a24090, 0x60c90f03)`, `SuperClassID = 0x1020`, `IsPublic = FALSE`) is feasible and likely cleaner than pure binary RE. Fallback Strategy 1 (scan loaded .max bytes for Class_ID, walk 342-byte stride, decode names from offset +34) is a ~50-line prototype if registration doesn't route. |
| 11b.1 | Modern multi-clip authoring + per-clip `.ala` emission (CLI/walker half of 11b). Walker change: introduce `ClipAnimation { name; AlaAnimation anim; }` in `scene_walker.h`; `walk_scene` now fills `vector<ClipAnimation>` instead of one `AlaAnimation`. Inner per-frame machinery extracted into `sample_clip_animation(start, end, ...)` (identical sampling — same rotation pass, same translation pool packing, same visibility threshold). New `walk_animations` dispatcher reads `Alamo_Anim_Clips` (pipe-delimited) for multi-clip; falls back to un-suffixed `Alamo_Anim_Start/_End/_Name` for single-clip back-compat. Exporter change: per-clip `.ala` emission with filename `<basename>_<CLIPNAME>.ala` (multi-clip) or bare `<basename>.ala` (single-clip). `.export.log` restructured: header `Animation: N clip(s) declared, M written` + one `[WROTE]/[FAILED]/[SKIPPED]` line per clip with the same per-clip metrics the old single-clip line had. Partial-export policy: malformed clip records (missing `_Start`/`_End`, inverted range) skip without crashing; other clips still emit. No format-library changes (Phase 8a was already multi-clip-ready — clip name lives in filename per Mike Lankamp's importer glob, not in the `.ala` bytes). | ✅ shipped | 5 new harness tests pass: `test_phase11_multi_clip` (3 clips WALK/ATTACK/IDLE with ranges 0-30/31-60/61-90 → 3 sibling `.ala` emit with correct n_frames + distinct rotation pools), `test_phase11_legacy_single_clip_back_compat` (un-suffixed `Alamo_Anim_*` → bare `<basename>.ala`, no suffixed siblings), `test_phase11_no_clips_no_ala` (no user props → no `.ala`, Phase 10b guard preserved), plus 2 tripwires: `test_phase11_tripwire_partial_export` (3 declared clips, 2 malformed [missing `_Start` + inverted range] → only the GOOD clip emits; the partial-export policy bites), `test_phase11_tripwire_precedence` (both conventions authored → multi-clip wins deterministically; FOO emits with the 11-frame `FOO_Start/End` range, NOT the 21-frame un-suffixed range — disambiguates which path fired). **RED-then-GREEN proved**: pre-implementation run of `test_phase11_multi_clip` against the un-modified `.dle` failed at assertion `#A2` (3 expected sibling `.ala` files missing), exactly the "feature missing" failure mode TDD requires. **11/11 prior tests preserved post-rebuild** (12th = `test_legacy_snowtrooper`, which needs the gitignored corpus and can't run from a worktree — verified non-regression via the static + animation regression sweep instead). 3× SHA-256 determinism: multi-clip outputs (`.alo` `9fe00a4e…`, WALK `e5faa889…`, ATTACK `88df1593…`, IDLE `1b8c5fd6…`) all byte-identical across runs. Phase 11b.2 (Utility-panel backend wiring) deferred to a follow-up session — CLI-side authoring via the Property Editor's user-prop pane is workable interim. |
| 11b.2 | Utility-panel backend wiring for Phase 11b. Connects the existing Animation Settings rollout (UI shipped Phase 7-era; backend stubbed) to Phase 11b.1's user-prop convention. **Selecting a clip in the combo scrubs Max's `animationRange` to that clip's [Start, End]** -- the user's explicit ask. **Editor data-flow model:** user props on rootNode are authoritative; combo selection loads picked clip's range into both spinners AND animationRange; spinner edits (CC_SPINNER_BUTTONUP only, not CC_SPINNER_CHANGE -- avoids per-drag undo spam) write back to props and re-apply animationRange; manual time-slider scrubs are PREVIEW only (don't mutate clip data). Add: modal `IDD_ALAMO_CLIP_NAME_PROMPT` sub-dialog with strict `[A-Z0-9_]+` validator + uniqueness check (inline error display; prompt stays open on rejection). Del: confirm via `MessageBox`, soft-delete via `_Start = -1` sentinel (Phase 10b's walker contract treats this as absent; works around SDK's missing `RemoveUserProp`). `<<`/`>>` use `navigate_clip_index` with wrap-around. Display Current re-snaps timeline to selected clip's stored range (idempotent under editor model; useful after manual scrub). Display All snaps timeline to `clip_range_union` across all clips. All mutations bracketed by `theHold.Begin()/Accept()` -- one ctrl-Z entry per user action ("Add Animation Clip", "Delete Animation Clip", "Edit Clip Start Frame", "Edit Clip End Frame"). File-open hook via `RegisterNotification(NOTIFY_FILE_POST_OPEN)` repopulates rollout after `File -> Open`; `NOTIFY_FILE_PRE_OPEN` + `NOTIFY_SYSTEM_PRE_RESET` clear the combo during transitions. CBN_SELCHANGE re-entrancy during programmatic `CB_SETCURSEL` suppressed via stack-scoped `SuppressComboGuard` flag. **Architecture split:** pure-C++ logic (parsing, validation, prev/next math, range union) lives in the format library at `alamo_format/{include,src}/anim_clip_list.{h,cpp}` -- no Max-SDK dependency, reusable by Phase 11c. The Max-side `max2alamo/src/animation_settings_dlg.{h,cpp}` is the thin Win32 glue. `alamo_utility.cpp` trims its old `AnimationSettingsDlgProc` stub (`-44 lines`) and gains `+~30 lines` of notification registration. **No walker or exporter changes** -- 11b.2 is strictly additive Utility-panel scaffolding on top of 11b.1. | ✅ shipped | **43 new Catch2 cases pass alongside existing 81 (124/124 total)** covering: `parse_clip_list` empty/single/multi/whitespace-trim/empty-field-drop semantics; `format_clip_list` round-trip; `validate_clip_name` 5 accept + 13 reject cases incl. real legacy name `ATTACKFLINCHB_00` from `EI_SNOWTROOPER.max`; `clip_start_prop_key`/`clip_end_prop_key` convention pinning; `navigate_clip_index` wrap-around in both directions; `clip_range_union` empty/single/contiguous/non-contiguous/overlapping. **3 unit-level tripwires fire on the expected assertion** (verified during implementation): D weakening regex `[A-Z0-9_]+` -> `[A-Za-z0-9_]+` fails "rejects lowercase" + "rejects mixed case"; E removing modulo in `navigate_clip_index` fails 3 wrap-related tests; F changing `format_clip_list` delimiter from `\|` to `,` fails the round-trip + multi-name tests. .dle builds clean against the Max 2026 SDK (one pre-existing-style C4100 warning, same shape as `alamo_utility.cpp`'s). Manual Tier 4 smoke checklist (15 steps) lives in `docs/build.md` under a new "Pre-release manual checks" section -- covers the user-requested combo-selection-scrubs-timeline behavior at step 5, the editor-model invariant at step 7 (manual time-slider scrubs don't mutate clip data), file-open-repopulation at step 13, and the Tripwire G manual mutation (remove `SetAnimRange` in `HandleClipSelected`; expect timeline NOT to scrub on combo nav). **Walker non-regression**: Phase 11b.1's 5 harness tests + 3 unit-level tripwire tests preserved post-rebuild (zero walker / exporter code changes). |
| 11c | Legacy `.max` clip-data reader (**Strategy 1** -- byte-scan; Strategy 2 / hidden ClassDesc deferred as a possible future refactor). The legacy Petroglyph max2alamo.dle Utility plugin stored per-clip animation metadata as 342-byte `appData` records keyed off `Class_ID(0x70a24090, 0x60c90f03)`. Phase 11c hooks `NOTIFY_FILE_POST_OPEN`, reads the just-loaded `.max` file from disk via `Interface::GetCurFilePath() + std::ifstream`, byte-scans for the legacy Class_ID, walks the 342-byte stride, and decodes per record: index at `+12` (uint32 LE), 16-byte ASCII name at `+34`, start frame at `+290` (uint16 LE), end frame at `+294` (uint16 LE) -- the last two pinned empirically by `re/scripts/extract_legacy_clip_ranges.py` via byte-differential on the simpler CIS_SBD records and cross-validated on EI_SNOWTROOPER + Stormtrooper. Translates records to Phase 11b's `Alamo_Anim_Clips` + per-clip `_Start/_End` user-prop convention on rootNode; the Animation Settings rollout repaints via the existing Phase 11b.2 `POST_OPEN` refresh -- no rollout changes needed. **Architecture split** (mirrors Phase 11b.2): pure-C++ scanner in the format library at `alamo_format/{include,src}/legacy_clip_scan.{h,cpp}` (Max-SDK-free, unit-testable), Win32 / Max-SDK glue in `max2alamo/src/legacy_clip_importer.{h,cpp}`. **Modern-wins precedence**: if rootNode already has `Alamo_Anim_Clips`, the importer is a no-op -- legacy → modern is one-way (save once via Phase 11b.2 authoring and the file is permanently pinned as modern). **Frame numbering**: legacy 1-based ranges preserved verbatim; frame 0 of Max's timeline is intentionally unused to match Mike Lankamp's `alamo2max.ms` importer's `base = 1` convention. **`animationRange` auto-extends** to cover imported clips so the user sees them immediately without needing to click Display All. **No walker or exporter changes** -- 11c is strictly additive on top of 11b. | ✅ shipped | **14 new Catch2 cases pass alongside existing 124 (138/138 total)** covering: empty/tiny/no-pattern inputs; single well-formed record at zero and non-zero file offsets; CIS_SBD-style 8-clip multi-record fixture asserting both per-record values AND the sequential-timeline invariant (each `start == prev_end + 1`); discovery-order vs index-field independence; malformed-record dropping (empty name, inverted range); end-of-buffer partial-record handling; end == start single-frame clips accepted. **3 unit-level tripwires** (H/I/J) fire on the corresponding 1-line offset/stride mutation. .dle builds clean against Max 2026 SDK; full plugin compiles with only the two pre-existing-style warnings (C4535 maxscript value_locals.h, C4100 in animation_settings_dlg.cpp). **Walker non-regression**: zero walker / exporter / format-library writer changes; Phase 11b.1's 5 harness tests + 3 tripwires + Phase 11b.2's 43 unit cases all preserved. Tier 4 manual smoke checklist (`docs/build.md` "Tier 4 — Legacy `.max` clip-data import (Phase 11c)") documents 8 in-Max validation steps using `CIS_SBD.max` (8 clips, 400 frames) + `EI_SNOWTROOPER.max` (60 clips, 2488 frames) + cross-tool round-trip through Mike Lankamp's importer + Tripwire H (mutate one byte of `kLegacyUtilityClassIdBytes` → expect 0 clips imported). |
| 12 | Shadow-volume closed-manifold validator. The Alamo engine renders stencil shadows from meshes whose material uses `MeshShadowVolume.fx` / `RSkinShadowVolume.fx`; the algorithm requires every edge shared by exactly two triangles (closed 2-manifold), otherwise the shadow has holes. The legacy Petroglyph `max2alamo` exporter warned (but didn't abort) on violation; Phase 12 reproduces that behaviour. **Architecture mirrors Phase 11b.2 / 11c**: pure-C++ topology check in `alamo_format/{include,src}/shadow_volume_check.{h,cpp}` (Max-SDK-free, unit-testable), Win32 / Max-SDK glue in `max2alamo/src/scene_walker.cpp::validate_shadow_volume_if_applicable`. Walker detects shadow-volume meshes by EITHER emitted shader name OR the legacy `_ALAMO_SHADOW_VOLUME` int user-prop; on detection, position-deduplicates the walker's emitted triangle list (ALO export splits vertices on UV/normal seams; index-space check would falsely flag closed meshes as open), builds an edge-incidence map, flags any edge with `count != 2` as non-manifold. Zero-area triangles silently skipped. Per-mesh warning surfaced to BOTH `.export.log` AND the MAXScript Listener: `WARNING: shadow-volume mesh '<name>' has <N> non-manifold edge(s); the engine's stencil shadow pass will render incorrectly.`. **Policy**: warn-only, never abort -- matches PG. **Diagnostic plumbing**: new `std::vector<std::string> warnings` field on `ExportMesh` for walker-side diagnostics; `alo_export.cpp` appends those lines into the `.export.log` after the existing material / scene summary blocks. **No format-library writer changes; no walker output changes for non-shadow meshes.** | ✅ shipped | **13 new Catch2 cases pass alongside existing 138 (151/151 total)** covering: empty / tiny / no-pattern inputs; closed tetrahedron + closed cube + subdivided cube; two disjoint closed tetrahedra; open cube (missing top face -> 4 boundary edges); T-junction (3 tris sharing one edge -> non-manifold); zero-area degenerate triangle filtering; canonical edge dedup (`{a,b}=={b,a}`); winding-independent topology check (pinned as a deliberate match of PG behaviour). **3 unit-level tripwires** (K canonicalization / L `count != 2` / M degenerate filter) fire on the corresponding one-line mutation. **3 MaxScript harness tests pass** (`test_phase12_shadow_closed_shader.ms`, `_closed_userprop.ms`, `_open_shader.ms`) verifying both detection paths and the no-false-positive contract. **Realistic mixed-content harness test** (`test_phase12_shadow_mixed.ms`: U-trough + closed cylinder, both with shadow shader) confirms only the UTrough warns. **Corpus baseline** (`scripts/check_shadow_volume_corpus.py`): 75.51% pass rate over 3,168 shadow-volume submeshes across 4,132 vanilla EaW + FoC `.alo` files -- ~25% "open" remainder is vanilla authoring noise PG would have warned on too; threshold locked at 75% as a regression alarm. **In-Max GUI Tier 4** (`docs/build.md` "Tier 4 -- Shadow-volume ... (Phase 12)") walked end-to-end via computer-use: closed `GUIClosedBox` -> 0 warnings in `.export.log`; open `GUIOpenBox` (one face removed) -> 1 warning naming the mesh + non-manifold edge count, export still succeeded. Exercises the `SceneExport::DoExport` GUI path (vs. the `exportFile` MAXScript path the batch harness uses). Plugin builds clean against Max 2026 SDK; full suite preserved post-rebuild. |
| 12.1 | Billboard orientation convention + `_ALAMO_BILLBOARDS` legacy back-compat. **Investigation** (no shipped binary changes for the convention itself; the engine already infers orientation from bone-local axes per AloViewer's `BillboardCorrection` matrix): document that all 8 `billboard_mode` values share one authoring rule -- **the billboard quad's visible-face normal must point along the bone's local -Y axis**. Established via two independent lines of evidence: (1) reading AloViewer source `src/RenderEngine/DirectX9/RenderEngine.cpp:256-266` (the `BillboardCorrection = Quaternion(Vector3(1,0,0), -90deg)` matrix maps bone-local -Y → camera-forward, with author comment *"They point to -Y but should point to +Z"*) plus `ObjectTemplate.cpp:48-69` for the per-mode dispatch (modes 1/2/6/7 apply the full correction; modes 3/4/5 preserve local +Z as the rotation axis and sweep the -Y front around it toward camera/sun/wind); (2) empirical corpus check via new `scripts/inspect_billboard_orientation.py` -- 14/14 vanilla PARALLEL + SUNLIGHT_GLOW meshes have average face normal at exactly `(0, -1, 0)` with tightness 1.000. **Shipped binary change**: `scene_walker.cpp::resolve_billboard_mode` helper -- modern `Alamo_Billboard_Mode` (0..7) wins; falls back to legacy `_ALAMO_BILLBOARDS = 1` MaxScript hook resolving to BBT_PARALLEL (mode 1) when the modern prop is absent. Older Petroglyph-tool-authored scenes (pre-radio-button era) that use the legacy hook now export with correct billboard mode instead of mode 0 / Disable. **No format-library changes; no walker output changes for any node that already had a modern prop.** | ✅ shipped | New MaxScript harness test `test_phase12_1_billboards_legacy_hook.ms` -- box with `_ALAMO_BILLBOARDS = 1` ONLY → exported synthetic per-mesh bone has `billboard_mode == 1`. Plus `inspect_billboard_orientation.py` empirical script committed under `scripts/`. Plugin builds clean. Full Catch2 suite preserved (151/151). Docs: `format-notes.md` gets a "Billboard pivot convention" subsection with the per-mode behavior table + the AloViewer source citation; `build.md` gets a "Tier 4 -- Billboard orientation convention (Phase 12.1)" section with 5-step in-AloViewer visual confirmation + Tripwire K; `alamo_utility.cpp` Help dialog extended with the -Y convention. **Open subquestions banked**: corpus presence of modes 3 (ZAXIS_VIEW, 0 hits) and 5 (ZAXIS_WIND, 7 hits) and 7 (SUN, 27 hits) was confirmed but not empirically inspected -- AloViewer source already extends the -Y convention to those modes; corpus inspection would be diminishing returns. |
| 9.3 | Post-merge investigation: Q6 (`_ALAMO_VERTEX_TYPE`) deep-dive + Phase 10 (per [#75](https://github.com/DrKnickers/max2alamo-2026/issues/75)) prereq plan. **Research-only**, no shipped binary changes. Triggered by [PR #72](https://github.com/DrKnickers/max2alamo-2026/pull/72) (v0.9 release notes) review pass: the claim that "automatic vertex-format inference works in practice" turned out to be wrong — [`alamo_format/src/alo_build.cpp:18`](../alamo_format/src/alo_build.cpp#L18) hardcodes `kVertexFormatName = "alD3dVertNU2"` for every submesh, and there is no inference. AloViewer source-code reading (`src/RenderEngine/DirectX9/{VertexManager,VertexFormats}.cpp`) shows the engine's renderer uses `_stricmp` lookup into a 15-entry `VertexFormatNames` table to bind the GPU vertex declaration — the string IS load-bearing, and our hardcode silently breaks skinned + bump + grass + vcolor exports (the basic-mesh declaration ignores the bone-slot + tangent data the walker already emits). The 4,929 / 4,929 corpus round-trip pass rate doesn't catch this because `alo_roundtrip` is read→write of vanilla bytes; the hardcode only affects fresh exports from our walker. New [`scripts/survey_vertex_formats.py`](../scripts/survey_vertex_formats.py) corpus surveyor confirms strict 1-to-1 shader↔format-string mapping in vanilla (10,737 submeshes / 60 unique triplets / 10 of AloViewer's 15-name set in use). **Bonus** — `0x10006` chunk reclassified from "vertex chunk variant" to "per-submesh skin-bone remap table" per AloViewer `Models.cpp:155`; 779 vanilla submeshes carry one, all on skinned shaders, our walker doesn't emit it (open question whether the engine requires it for correct skinned rendering; tracked as Risk 8 of #75). Phase 10 plan filed at #75 with 3-PR sequencing (A: writer string field; B: stock-shader table + walker; C: per-mesh override + Tier 4 docs); prereq Tier 4 protocol at [`docs/plans/phase-10-prereq.md`](plans/phase-10-prereq.md) covering the two open empirical questions (R7 engine-vs-AloViewer binding rule, R8 0x10006 requirement). R1 already resolved from AloViewer source code (default skinned shaders to `alD3dVertB4I4*` rather than `alD3dVertRSkin*` since our walker fills 4 bone slots and RSkin reads only slot 0). Numbering note: "Phase 10" in #75 is independent of the in-binary Phase 10 row above (legacy `.max` compatibility); the issue title was chosen before the conflict was noticed and can be renamed if it causes confusion. | ✅ shipped (research-only) | `docs/format-notes.md` Q6 + new Q6b resolved with AloViewer source citations + corpus evidence; MAXScript hooks table now points at #75 for the override workflow. [PR #72](https://github.com/DrKnickers/max2alamo-2026/pull/72) merged (`2fb9bc5`) — v0.9 release notes with the three accuracy fixes (round-trip count 2,066 → 4,929, dead `eaw.petrolution.net` URL → live `modtools.petrolution.net`, honest framing of the writer's fixed-string emission limitation). [PR #76](https://github.com/DrKnickers/max2alamo-2026/pull/76) merged (`74b629b`) — the investigation docs + corpus surveyor tool. [Issue #75](https://github.com/DrKnickers/max2alamo-2026/issues/75) body rewritten with resolved scope. v0.9.0 tag + `.dle` build + publish remains open (manual ceremony per [`docs/release.md`](release.md)). |
| 13a | Phase 10 ([#75](https://github.com/DrKnickers/max2alamo-2026/issues/75)) — vertex-format string plumbing per shader, half-shipped. New `alamo_format::vertex_format_selector` module at [`alamo_format/{include,src}/vertex_format_selector.{h,cpp}`](../alamo_format/include/alamo_format/vertex_format_selector.h) with a 42-entry stock-shader → format-name table (corpus-empirical + naming-inferred) and a case-insensitive validator against AloViewer's 15-entry `VertexFormatNames` set. `ExportSubmesh::vertex_format_name` field added; pre-Phase-10 hardcode at [`alo_build.cpp:18`](../alamo_format/src/alo_build.cpp#L18) replaced with the per-submesh value (empty = back-compat fallback to `alD3dVertNU2`). Walker populates the field per submesh via `default_vertex_format_for_shader(shader_name)`. Numbering note: "Phase 10" in #75 collides with the in-binary Phase 10 row (legacy `.max` compatibility); landed here as Phase 13a since 12.1 was the prior shipped binary phase. **Empirical follow-up — half the fix only.** User re-exported `EI_SNOWTROOPER.alo` post-PR-#80 and rendered in AloViewer: skinned submeshes (Snowtrooper_Cape on `RSkinGlossColorize.fx`) still produce stretched-triangle artifacts despite the correct `alD3dVertRSkinNU2` string. Confirms #75's Risk R8 was load-bearing: the `0x10006` per-submesh skin-bone-remap chunk is required for the renderer to bind bone references correctly. R8 carved into [#81](https://github.com/DrKnickers/max2alamo-2026/issues/81) for the second-half writer + walker work. | ✅ shipped (half) | 7 new Catch2 cases in [`vertex_format_selector_test.cpp`](../alamo_format/tests/vertex_format_selector_test.cpp) (corpus-empirical mappings incl. the Snowtrooper-shader case, inferred mappings, unknown-shader handling, case-insensitivity, full 15-entry recognized set, typo rejection, table-coverage cross-check). 1 new wiring case in [`alo_build_test.cpp`](../alamo_format/tests/alo_build_test.cpp) with 3 SECTIONs asserting empty-field fallback, populated-field passthrough, multi-submesh independence. Full suite **168/168** (was 159/159). [PR #80](https://github.com/DrKnickers/max2alamo-2026/pull/80) merged at `56b0efb`. Acceptance criterion (Snowtrooper renders correctly) **NOT met** by this PR alone — completion gated on #81's `0x10006` writer. |
| 9 | Polish + release plumbing. **v0.9.0** chosen as the first pre-1.0 tag -- signals "format library + Max plugin feature-complete, awaiting v1.0 commitment after a stabilization window". Three pieces of plumbing: (1) **single-source-of-truth version** via `project(VERSION ...)` in the top-level `CMakeLists.txt` propagated through CMake's `configure_file` to a generated `alamo_format/include/alamo_format/version.h`; both CLIs read from there for `--version` output. (2) **Release CI workflow** at `.github/workflows/release.yml` triggers on `v*.*.*` tag push -- builds Release/x64, runs `ctest`, verifies CLI `--version` matches the tag (drift detector), extracts the matching `CHANGELOG.md` section, creates a **draft** GitHub Release with `alo_dump.exe` + `alo_roundtrip.exe` + `alamo_format.lib` attached. Pre-release tags (`-rc.N`, `-beta.N`) are auto-marked as pre-releases. (3) **`CHANGELOG.md`** at repo root in Keep-a-Changelog format, seeded with the v0.9.0 entry grouping every phase since project inception by area (format library / Max plugin / testing / docs). **Manual ceremony** documented in new `docs/release.md`: after the workflow creates the draft, the maintainer builds the `.dle` locally from the tagged commit, attaches it to the draft, and publishes -- the Max SDK is non-redistributable so CI cannot ship the plugin. Includes a tag-rollback protocol for pre-publish failures. | ✅ shipped (plumbing) | Plugin builds clean against Max 2026 SDK; full Catch2 suite preserved (151/151, 827 assertions). `alo_dump --version` + `alo_roundtrip --version` both report `0.9.0` from the configure_file-generated header. Release workflow scoped via tag pattern `v[0-9]+.[0-9]+.[0-9]+` (+ optional pre-release suffix); does not fire on every push. Once this PR merges, the **v0.9.0 tag itself** is the trigger -- see `docs/release.md` for the manual procedure to cut the actual release. Phase status pinned `shipped (plumbing)` -- the binary release is a follow-up action; once v0.9.0 is tagged + the `.dle` is published, this row gets `✅ shipped` with the release URL. |

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
| Feature wishlist (pre-scope) | `docs/wishlist.md` -- captured ideas not yet promoted into a phase plan |
| Per-release changelog | `CHANGELOG.md` (Keep a Changelog format; section per version) |
| Release procedure | `docs/release.md` -- tagging, manual `.dle` build + attach, draft-publish dance |
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

Current coverage (29 tests, all green):

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
| `test_rotation_keyframes` | Phase 8b — single bone keyframed 0°→90° about Z over 31 frames; emits sibling `.ala` with FoC framing + `0x1009` rotation pool. **17-assertion verifier** covers chunk structure, FoC mini-chunks, pool size, idx_rotation per bone, frame[0] ≈ identity / frame[30] ≈ 90°-Z within 1e-3, unit-length quats, sign-canonicalisation continuity. **Updated for Phase 8c** to expect a translation pool on the animatable bone (constant position → degenerate `trans_scale=(0,0,0)`). |
| `test_translation_keyframes` | Phase 8c — 2-bone scene: TransBone keyframed [0,0,0]→[10,20,5], RotBone with constant position + 0°→90° Z rotation. Emits sibling `.ala` with both `0x100a` (372 B) and `0x1009` (496 B) pools. **25-assertion verifier** across 4 groups (structural, slot assignment, rotation, translation); covers per-bone offset/scale round-trip, frame[0] / frame[30] decoded positions within 1e-3, constant-axis degenerate-scale handling, sign-canonicalisation continuity |
| `test_visibility_blinking_light` | Phase 8d — Omni light keyframed visible@frame 0 / hidden@frame 30, plus an `AlwaysVisible` BoneSys bone with no visibility animation, plus an `AnchorBox` mesh. Emits a `0x1007` leaf inside BlinkingLight's per-bone `0x1002` container. **15-assertion verifier** across 3 groups (structural / chunk emission / bit-packing semantics + monotonicity). The test does NOT pin a magic byte sequence — Max 2026's on_off visibility controller can't be replaced with a clean bezier_float via any documented MaxScript path, and `INode::GetVisibility(t)` returns a float that crosses 0.5 at a tangent-dependent frame. Instead, the verifier pins what's invariant: chunk presence, payload size = `ceil(n_frames/8)`, frame 0 visible (at TRUE key), frame n_frames-1 hidden (at FALSE key), transition exists and is monotone visible-then-hidden in LSB reading order (catches MSB-first regression), trailing unused bits clear, AlwaysVisible elided, no other bone has a `0x1007`. Note: `.ala`'s `0x1007` is binary by format design (1 bit per frame); the walker's `>= 0.5` threshold means intermediate Max visibility values are lossy at export — Max supports float visibility, the format does not |
| `test_phase8_acceptance` | Phase 8e — full animation-surface integration. 11-bone / 2-mesh / 2-light / 2-proxy scene mirrors Phase 7d's static composition plus animation overlay: 4 bones with rotation/translation tracks (BoneA Z rotation, BoneB translation Δ=(5,10,15), HelperBone X rotation, PivotedHardpoint Z rotation atop static -90° Y `objectoffsetrot`) and 3 bones with visibility tracks (OmniLight + StaticBox visible→hidden, prox0 asymmetric hidden→visible). SpotLight + SpotLight.Target + prox1 are control bones with no animation. **62-assertion verifier** across 10 groups; ~62/62 passes. Pin-points: bone_map enforcement (rot_words=16, trans_words=12, no spurious slot assignments on visibility_map-only bones), 5g×8b composition (per-frame quaternion = `q_offset * q_authored`), visibility direction-symmetry (monotone visible-then-hidden AND monotone hidden-then-visible both accepted), constant-visible elision (3 bones emit 0x1007 + 4 control bones don't), skinning data still byte-clean despite animation. Hard merge gate: all 10 prior gold SHAs preserved. New 8e gold: `.alo` `dbc4f82b…`, `.ala` `99a74727…` |
| `test_phase8_visibility_propagation` | Phase 8f — regression test for the Utility-panel "Propagate visibility to descendants" button. ParentBone gets animated visibility; 3 children (Child1/2/3) parented to it; OutsideSubtree parented to Root. After running the propagation snippet (same code the C++ button invokes via ExecuteMAXScriptScript), the 3 children's 0x1007 payloads must be byte-identical to ParentBone's, while OutsideSubtree (outside the selected subtree) must have NO 0x1007. Pins both "propagation correctly copies the visibility controller" and "propagation scope is limited to descendants of the selected node." 7/7 assertions. |
| `test_phase8_mesh_hierarchy` | Phase 8f — pins the corpus-evidence-locked LOCAL-ONLY visibility convention. Three-box chain (BoxParent → BoxMid → BoxLeaf) + a bone parented to BoxMid. BoxParent is hidden in Max. Asserts that on disk: BoxParent has `bone.visible = false`, but BoxMid, BoxLeaf, and LinkedBone all have `bone.visible = true` (the SDK's per-node `IsNodeHidden()` doesn't propagate; the corpus survey of 2066 vanilla files showed 0 ancestor-inheritance evidence; Mike's and Gaukler's importers agree). 8/8 assertions. **Surfaced walker bug**: static-mesh synth bone was hardcoded `visible=true`; fixed in-PR to honor `IsNodeHidden()`. |
| `test_phase8_name_round_trip` | Phase 8f — UTF-8 byte-for-byte round-trip of bone names. Chinese (`Hone1_骨头`), Japanese katakana (`Hone2_テスト`), Cyrillic (`Hone3_тест`), and a 100-char Latin name. Catches `to_utf8()` converter bugs (CP_ACP vs CP_UTF8) and length-truncation. 7/7 assertions. |
| `test_phase8_anim_single_frame` | Phase 8f — 1-frame clip (`Alamo_Anim_Start=0, Alamo_Anim_End=0`). Catches off-by-one bugs in the per-frame loops. **Surfaced walker bug**: `walk_animation` rejected `end == start` clips (gate was `<=` instead of `<`); fixed in-PR to allow single-pose animations. 3/3 assertions. |
| `test_phase8_anim_long` | Phase 8f — 301-frame clip with rotation 0°→360°. Pool sizes scale linearly to 2408+1806 bytes. Catches integer-overflow in pool-size computation at high frame counts. 4/4 assertions. |
| `test_phase8_anim_60fps` | Phase 8f — 61-frame clip at 60 fps. Pins fps round-trip through the .ala info mini-chunk's float32 slot. 2/2 assertions. |
| `test_phase8_object_offset_scale` | Phase 8f — pins the 5g caveat that `objectoffsetscale` is silently dropped (Phase 5g composes rot + pos but not scale). Authors two helpers with non-identity scale offsets ([3,1,1] and [2,2,2]) and asserts the on-disk bone matrix basis rows stay UNIT-LENGTH, proving the scale offset was NOT composed. Tripwire (scale composition) would fire the unit-length check. |
| `test_phase8_biped_animated` | Phase 8f — `biped.createNew` rig + cylinder skinned to pelvis + spine. Catches "biped bones don't appear in scene.bones" or "biped TM convention differs from BoneSys" regressions. 35 biped bones round-trip; skin data unchanged. 5/5 assertions. Animation deferred (biped pelvis requires `biped.setTransform` API). |
| `test_phase8_skinning_stress` | Phase 8f — 32-bone chain + cylinder skinned to all of them (top-4 picker per-vertex). 1584 verts, 32 bones referenced, all weight sums = 1.0. Catches "skin pipeline has hidden bone-count assumption" regressions. |
| `test_phase8_uv_distortion` | Phase 8f — three boxes with tiled (4×), negative (-2×), and mirrored UV layouts + bump-mapped material. Catches NaN/Inf in tangent/binormal/UV/position vectors under degenerate UV authoring (MikkT stability under extreme inputs). 108 verts; no NaN/Inf. |
| `test_phase8_high_vertex_mesh` | Phase 8f — GeoSphere with 60 segments, 43200 verts post-tessellation in a single submesh. Catches arithmetic overflow in vertex-count-related size computation paths. Under the 65535 uint16 ceiling enforced by `alo_export.cpp:128`. |
| `test_phase8_geometry_edge_cases` | Phase 8f — three meshes: NoMaterialMesh (no material assigned) and MultiMaterialMesh (Multi/Sub-Object with 2 sub-materials). Pins "walker doesn't crash on missing material" and "multi-submesh dispatch works." |
| `test_pivot_node_rotation` | Phase 5g sanity guardrail — BoneSys + Dummy each rotated via `node.rotation = eulerAngles 0 0 -90`. Verifier asserts both bone matrices have col1 ≈ (-1, 0, 0) within 1e-3 (rotation captured via the node-TM path, which has always worked). Catches any regression in the new compose helper that would break node-level rotation |
| `test_pivot_affect_only` | Phase 5g regression test (issue [#53](https://github.com/DrKnickers/max2alamo-2026/issues/53)) — Dummy with `objectoffsetrot = quat -90°-about-Z`, the canonical hardpoint authoring pattern. Asserts col1 ≈ (-1, 0, 0) within 1e-3. **Fails before Phase 5g; passes after** |
| `test_pivot_skinned_safety` | Phase 5g skinning regression guard — 2-bone chain (B0 with `objectoffsetrot`, B1 identity) + cylinder skinned with rigid per-row binding. Asserts B0 carries the offset (non-identity col1), B1 stays identity, all 144 vertex weight sums = 1.0 within 1e-3, every bone reference resolves. Guards against the speculative regression where applying the fix to skinning bones would distort vertex deformation |

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

### Phase 5g — Pivot-orientation fix (shipped, closes [#53](https://github.com/DrKnickers/max2alamo-2026/issues/53))

Walker was writing bone matrices via `IGameNode::GetLocalTM()` / `GetWorldTM()`, neither of which include the object-offset transform (Max's `objectoffsetrot` / `objectoffsetpos` / `objectoffsetscale`). When a user authored a hardpoint direction via **Hierarchy → Affect Pivot Only**, the rotation was silently dropped — the on-disk `0x206` bone matrix came out identity regardless of authored direction. The engine reads the bone matrix's "forward" axis for firing-cone projection; without the fix, weapons fired along world +Y regardless of authored pivot direction.

The fix: a new helper `compose_with_object_offset(INode*, Matrix3) → Matrix3` that composes the node TM with the object offset via `INode::GetObjOffsetPos/Rot()`. Composition order is `offset_TM * node_TM` (row-vector convention: offset applied first to convert pivot-space to node-space, then node TM to convert to parent-space). Applied at **seven callsites** in `max2alamo/src/scene_walker.cpp`:

| Site | Phase | What |
|---|---|---|
| `walk_bones` (line 603) | 5a / 5e | Real bones + helpers-as-bones |
| `walk_lights` (line 734) | 7b | Synthetic per-light bone |
| `walk_lights` (line 754) | 7b.2 | Spotlight `.Target` sibling bone |
| `walk_proxies` (line 835) | 7c.2 | Synthetic per-proxy bone |
| `walk_node` (line 895) | 4c | Static-mesh attachment bone |
| `walk_animation` (line 1230) | 8b | Per-bone `default_rotation` sample |
| `walk_animation` (line 1267) | 8b / 8c | Per-frame rotation + translation extraction |

For identity object offset (the case for every existing harness test and every vanilla corpus file) the composition is a no-op: `offset_TM * node_TM == node_TM` byte-identical. This was verified empirically by re-exporting the three gold-reference tests through the new plugin and confirming the SHAs match pre-fix exactly:

- `test_phase7_acceptance` → SHA `56FCEAF02E3D043E…` (matches Phase 7d baseline; re-verified post-8d)
- `test_rotation_keyframes` → SHA `3791F274…` (matches Phase 8b .alo baseline; re-verified post-8d)
- `test_translation_keyframes` → SHA `A0C333A2…` (matches Phase 8c baseline; re-verified post-8d)

**`scale` is intentionally not composed.** Non-unit object-offset scale is exotic; none of the corpus or tests exercise it; the runtime semantics of a scaled bone matrix would require additional thought.

**Two caveats documented for users:**

1. **BoneSys bones double-apply `objectoffsetrot`.** Setting `b.objectoffsetrot = quat -90°-Z` on a `BoneSys.createBone` produces a bone matrix carrying 180° of rotation, not 90° — Max's BoneSys class interacts with the object offset via internal logic that composes twice. The workaround is to rotate BoneSys bones via `b.rotation = …` (node-level) or the interactive Rotate tool in Normal mode, NOT Affect Pivot Only. Dummy/Point helpers — the canonical hardpoint authoring pattern — work correctly and round-trip the authored offset.

2. **`BoneSys.createBone <from> <to>` direction args are NOT recoverable.** The bone's procedural-display direction lives in bone-object internal data; there's no Max SDK accessor for it. Users authoring bones in MaxScript should set rotation explicitly (`b.rotation = …`) instead of relying on createBone's direction args alone. Interactive bone-drawing in the viewport produces a node TM with rotation built-in, so this caveat only affects programmatic bone creation.

Three new harness tests added, with the existing 26 unchanged:

- `test_pivot_node_rotation` (5g sanity guardrail) — confirms `b.rotation` / `d.rotation` continues to round-trip.
- `test_pivot_affect_only` (5g regression test) — Dummy with `objectoffsetrot = quat -90°-Z`; verifier asserts col1 ≈ (-1, 0, 0) within 1e-3. **Fails before the fix; passes after.**
- `test_pivot_skinned_safety` (5g skinning guard) — 2-bone chain (B0 with offset, B1 without) + cylinder skinned with rigid per-row binding. Verifier asserts B0 captures the offset, B1 stays identity, weight-sum = 1.0 per vertex. Catches any regression where applying the fix to skinning bones would distort vertex deformation.

Pinned signals at merge:
- 29/29 harness · 81/81 unit · 4929/4929 chunk-tree · 2066/2066 .alo strict · 2863/2863 .ala typed-pipeline — all unchanged.
- 3 negative tripwires fire: dropping the compose-helper call at site 603 → affect_only fails (but node_rotation still passes); forcing identity offset in the helper → affect_only fails; reversing composition order → didn't bite for this test data (node TM was identity, so order is invisible — documented as a known limitation of the tripwire).

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

### Phase 8c — Walker translation tracks (shipped)

Extends `walk_animation()` to also sample per-frame `Matrix3::GetRow(3)` (the translation row of each bone's `GetLocalTM(t)`) and pack it into a file-scope `0x100a` translation pool alongside the `0x1009` rotation pool. The single `GetLocalTM(t)` call per (bone, frame) is shared with rotation extraction — no extra Max overhead. Per-bone packing:

```
trans_offset[axis] = min over frames of pos[f].axis
trans_scale[axis]  = (max - min) / 65535     (clamped to 0 if range < 1e-9)
packed[f].axis     = round((pos[f].axis - offset) / scale)  in [0, 65535]   (or 0 if scale==0)
```

Pool elements are stored as `int16` (mirroring the read-side `translation_pool` typing); `uint16` semantics apply on the unpacker side via the per-bone offset+scale transform. Constant-axis bones (where `max == min`) get `trans_scale = 0` and packed values of 0; at runtime the unpacker yields `0 * 0 + offset = offset`, which is the correct constant.

**Scale tracks are deliberately not emitted.** A pre-design corpus survey found **0 of 1500 FoC vanilla `.ala` files** have `nScaleWords > 0`, and the format spec defines no chunk ID for a file-scope scale pool. 8c sets `n_scale_words = 0`, leaves `idx_scale = -1` on every bone, and leaves the per-bone `scale_offset` / `scale_scale` mini-chunks 8/9 at their struct defaults. The `AlaBoneTrack` fields for scale are vestigial relics of an unused format facility.

8c also extends Phase 8b's `verify_test_rotation_keyframes.py` for forward compatibility: every animatable bone now gets a translation track even when its position is constant, so the 8b verifier was updated to expect `n_translation_words == 3` and a `0x100a` chunk (constant-position case → degenerate `trans_scale=(0,0,0)`).

The `.export.log` "Animation:" line now reports both `rotation word(s)` and `translation word(s)`.

Pinned signals:
- New harness test `test_translation_keyframes` (2-bone scene: TransBone keyframed `[0,0,0] → [10,20,5]`, RotBone with constant position + `0° → 90°` Z rotation). 25-assertion verifier covers structural shape, slot assignment, rotation correctness, translation correctness.
  - `TransBone.trans_offset ≈ (0, 0, 0)`, `trans_scale ≈ (10/65535, 20/65535, 5/65535)`.
  - Frame[0] decoded position ≈ `(0, 0, 0)` within `1e-3`; frame[30] ≈ `(10, 20, 5)` within `1e-3`.
  - `RotBone.trans_scale = (0, 0, 0)` exercises the divide-by-zero guard; frame[0] and frame[30] decode to the same constant position.
- Full harness: 26/26. Existing 24 static tests still produce only `.alo` (no spurious `.ala`).
- Phase 7d byte-identity check after plugin rebuild: SHA still `56FCEAF02E3D043E…`.
- 3× byte-identical determinism on the new test: `.alo` SHA `A0C333A2…`, `.ala` SHA `749EBCE8…`.
- Non-regression: 4929/4929 chunk-tree · 2066/2066 .alo sweep · 2863/2863 .ala typed-pipeline · 81/81 unit — all unchanged.
- Three negative tripwires: drop the frame-30 translation keyframe → `trans_scale=(0,0,0)` and frame[30] decodes to (0,0,0) (#D21/#D23); swap `trans_scale` axes 0↔1 in writer → per-axis scale values wrong (#D21/#D23); corrupt `n_translation_words` off-by-3 → mini-chunk 12 wrong + `0x100a` size mismatch (#A7/#A10). All reverted post-demonstration.

### Phase 8d — Walker visibility tracks (shipped)

Closes the first-class v1 animation goal from the Phase 8 frame plan: object visibility tracks. `walk_animation` gains a third pass that emits a `0x1007` chunk inside each per-bone `0x1002` container when the bone's source Max INode has animated visibility. The bit-pack convention matches `docs/format-notes.md:454-477` (LSB-first per byte, bit SET = visible) — empirically confirmed by `alo_dump` of FoC `AI_RANCOR_ATTACK_00.ALA`, which has a `0x1007` leaf nested inside one of its `0x1002` containers despite the format-notes bracketing implying the chunk is EaW-only.

**Architectural key: two parallel maps from INode\* to bone index.**

| Map | Populated by | Used by | Why distinct |
|---|---|---|---|
| `bone_map` | `walk_bones` (real bones + helpers-as-bones) | Skin resolver, rotation/translation track emission | Keeping it narrow preserves the 8b (`3791F274…`) and 8c (`A0C333A2…`) gold .alo SHAs — broadening it would expand rotation/translation slot assignment to light/proxy/static-mesh synth bones |
| `visibility_map` | `walk_bones` + `walk_lights` (light + `.Target`) + `walk_proxies` + `walk_node` (static-mesh attachment branch only) | `walk_animation` visibility third pass | Broader surface for per-frame visibility sampling without contaminating the rotation/translation surface |

`visibility_map` is allocated locally in `walk_scene` and threaded as a fourth ref-arg through every sub-walker that emits a bone with a Max-side source INode. Public `walk_scene` signature is unchanged (per the plan: "threaded locally... not in scene_walker.h").

**Visibility sampling and threshold.** For each `(INode*, bone_index)` in `visibility_map`, the third pass samples `INode::GetVisibility(t, nullptr)` per integer frame in `[start, end]`. Each float sample is thresholded at `>= 0.5` to produce a per-frame bool. If at least one frame is hidden, a `0x1007` leaf is appended to `out_anim.bones[bone_index].track_leaves`; otherwise the bone is elided (constant-visible bones emit no track, matching vanilla convention). Phase 8a's `AlaBoneTrack::track_leaves` field carries the chunk verbatim through `build_ala()` — no format-library changes.

**Format-vs-runtime: binary on disk, float in Max.** Max itself supports intermediate visibility values — `INode::GetVisibility(t)` returns a float in `[0, 1]` that the Max runtime, materials, and shader pipeline all honor for fade-in/fade-out effects. The Alamo `.ala` format does **not**: `0x1007` is a bit-packed mask, one bit per frame, with no intermediate values possible. That's a hard format constraint, not a workaround. The walker's `>= 0.5` threshold collapses Max's float to binary at export time — **authoring a continuous fade in Max is lossy through this code path**. Users wanting real shader-level alpha animation in EaW need to drive shader parameters (the `IDxMaterial` ParamBlock path), not the bone-visibility track; that workflow is Phase 9 polish territory and out of scope for v1's `0x1007` emission.

Related (orthogonal) gotcha discovered during 8d: Max 2026 installs an `on_off` *Boolean* controller on every freshly-created node's Visibility track, and none of the documented MaxScript paths for replacing it with a `bezier_float` succeed (see the "test scene caveat" subsection below). The `on_off` controller in Max 2026 still bezier-interpolates between Boolean keys (so `GetVisibility(t)` returns a smoothly-varying float despite the controller's name), which is fortunate — it's what the test scene relies on to produce an interpolated transition for the threshold to consume. But there's a real gap here for MaxScript authors who want to write float-valued visibility keys directly: no working pattern was found. Given the format collapses to binary anyway, this is mostly a test-authoring ergonomic problem rather than a format-fidelity issue.

`pack_visibility_bits(const std::vector<bool>&)` is a free helper near the other animation packers:

```
byte_idx 0 bit 0 = frame 0
byte_idx 0 bit 7 = frame 7
byte_idx 1 bit 0 = frame 8
...
output size: ceil(n_frames / 8) bytes
trailing bits in the last byte are 0
```

**Skinned meshes have no per-mesh attachment bone.** Per the Phase 5b/5c design, skinned meshes connect to Root with each vertex referencing real bones. Their visibility comes from the bones they skin to (already in `visibility_map` via `walk_bones`). Animating `mesh.visibility` on a skinned mesh has nowhere to land at the bone level — documented limitation; users wanting "blink the whole mesh" animate its skinning bones or use shader-level alpha (Phase 9 polish).

**`Alamo_Geometry_Hidden` is orthogonal.** Phase 5d's static `is_hidden` field on the `0x402` chunk and Phase 8d's per-frame `0x1007` are independent semantics. A mesh can be statically hidden AND have animated bone visibility; the engine combines them at runtime — out of scope for the exporter.

**One pre-existing exit removed.** `walk_animation`'s previous `if (rot_next == 0) return;` early-exit would have skipped the visibility pass for scenes with animated visibility but no rotation/translation tracks (e.g. a lone blinking light). Replaced with a `if (rot_next > 0) { allocate rotation_pool; }` gate, leaving the rest of the function gated on `idx_rotation < 0` / `trans_next > 0` per bone — no behavior change for existing scenes.

**`.export.log` enhancement.** The "Animation:" line now reports the count of bones with visibility tracks. `has_anim_track` (the gate that decides whether to write the sibling `.ala`) also now checks `!track_leaves.empty()` so a visibility-only scene still emits the `.ala`.

**Why the test's expected payload is invariant-based, not byte-pinned.** Max 2026 installs an on_off Boolean controller on every freshly-created node's visibility track. **None of the documented MaxScript patterns for replacing it with a clean `bezier_float` succeed** (`ol.visibility = bezier_float()`, `ol[#Visibility].controller = bezier_float()`, `removeVisibilityTrack` + `addVisibilityTrack`, `setVisController`). And `INode::GetVisibility(t)` samples the on_off controller as a float that bezier-interpolates between keys — so the exact transition frame is Max-implementation-defined. The test scene authors `at time 0 (true)` + `at time 30 (false)`, and the verifier pins what's invariant under any reasonable controller behavior:

- BlinkingLight has a `0x1007` leaf of size `ceil(n_frames/8) = 4`
- frame 0 (byte 0 bit 0) is SET (at the TRUE key)
- frame n_frames-1 (byte 3 bit 6) is CLEAR (at the FALSE key)
- transition exists AND is monotone visible-then-hidden in LSB iteration order — this catches MSB-first regressions (which Max's actual byte pattern `[FF 03 00 00]` would mask in a frame-0-only check, since frame 7 is also visible)
- trailing unused bits (frame_index >= n_frames) are clear
- AlwaysVisible BoneSys bone has NO `0x1007` (constant-visible elision)
- No other bone has `0x1007`

15 assertions across 3 groups (structural / chunk emission / bit-packing semantics + monotonicity).

Pinned signals:
- Full harness 30/30. The 24 pre-Phase-8 tests still produce only `.alo` (no spurious `.ala`); 5 animation tests (`test_rotation_keyframes`, `test_translation_keyframes`, `test_visibility_blinking_light`) produce `.ala` siblings.
- **Hard merge gate (visibility pass is a true no-op for non-visibility scenes):**
  - `test_phase7_acceptance` → SHA `56FCEAF0…` (Phase 7d gold)
  - `test_translation_keyframes` → SHA `A0C333A2…` / `.ala` `749EBCE8…` (Phase 8c gold)
  - `test_rotation_keyframes` → SHA `3791F274…` (Phase 8b .alo gold)
  - `test_pivot_node_rotation` / `test_pivot_affect_only` / `test_pivot_skinned_safety` → SHAs `0919e2df…` / `00b21377…` / `c9466af6…` (Phase 5g)
  - The plan's `test_rotation_keyframes.ala` SHA `2B60515B` is a stale 8b-era value that was already invalidated when 8c added translation pools to every animated bone. The current stable post-8c SHA is `19a79cbb…`, reproduced byte-identically across re-exports — confirming 8d is a no-op for this test.
- 3× byte-identical determinism on the new test: `.alo` `f2f027ff…`, `.ala` `7fe1478a…`. (Despite `visibility_map`'s `std::unordered_map` iteration order being unspecified, output bytes are deterministic because per-bone emission goes into `out_anim.bones[bone_idx].track_leaves`, which `build_ala()` walks linearly.)
- Non-regression: 4929/4929 chunk-tree · 2066/2066 .alo strict sweep · 2863/2863 .ala typed-pipeline · 81/81 unit — all unchanged.
- Three negative tripwires (all reverted post-demonstration):
  - **A** — remove the `animate on (...)` block → BlinkingLight constant-visible → walker elides → verifier #B7 fires (`BlinkingLight has no 0x1007 leaf`).
  - **B** — flip `pack_visibility_bits` to MSB-first (`1u << (7 - (f % 8))`) → verifier #C14 monotonicity fires (`hidden first appears at frame 8 but a visible bit reappears at frame 14`); confirms #C14 actually distinguishes LSB from MSB even when our specific bit pattern happens to set both bit 0 and bit 7 of byte 0.
  - **C** — invert bit semantics (`visible[f] = !is_visible`) → #C12 (frame 0 expected SET, got CLEAR) + #C13 (frame 30 expected CLEAR, got SET) + #C14 (non-monotone) all fire.

### Phase 8e — Full animation-surface integration acceptance (shipped)

Mirrors Phase 7d's static-surface acceptance test, but with the animation surface (rotation + translation + visibility + pivot-orientation) overlaid on top. **No walker changes** — pure test addition. The point is cross-feature interaction coverage: per-feature tests (8b/8c/8d/5g) pin each surface in isolation; 8e proves they compose correctly when the walker sees all of them in one pass.

**Scene composition (11 bones, 2 meshes, 2 lights, 2 proxies):**

| # | Bone | Source | bone_map | visibility_map | Animation |
|---|---|---|---|---|---|
| 0 | Root | (synthetic) | no | no | — |
| 1 | BoneA | Real Max bone | ✅ | ✅ | Rotation 0° → 90° Z (8b) |
| 2 | BoneB | Real Max bone (sibling) | ✅ | ✅ | Translation Δ=(5, 10, 15) (8c) |
| 3 | HelperBone | Dummy + `Alamo_Export_Transform` | ✅ | ✅ | Rotation 0° → 45° X (8b on helper-as-bone) |
| 4 | PivotedHardpoint | Dummy + `objectoffsetrot = -90° Y` + animated | ✅ | ✅ | Rotation 0° → 60° Z + static -90° Y offset (5g×8b cross) |
| 5 | StaticBox | Per-mesh attachment bone | no | ✅ | Visibility T→F (8d on static-mesh attachment) |
| 6 | OmniLight | Per-light synth bone | no | ✅ | Visibility T→F (8d on light) |
| 7 | SpotLight | Per-light synth bone | no | ✅ | NONE — control |
| 8 | SpotLight.Target | Sibling bone | no | ✅ | NONE — control |
| 9 | prox0 | `Alamo_Proxy` helper | no | ✅ | Visibility F→T (asymmetric direction test) |
| 10 | prox1 | `Alamo_Proxy` helper | no | ✅ | NONE — control |

Plus a skinned cylinder bound rigidly to BoneA + BoneB. BoneA and BoneB are SIBLINGS (both root-level), not a parent-child chain — this keeps BoneB's local-TM translation samples clean of BoneA's per-frame rotation, which would otherwise compose into the child's local frame and complicate the verifier's #F35 delta assertion.

**Key authoring lessons:**
- **BoneSys.createBone start-position quirk.** Authoring BoneB at `[0, 0, 0]` (matching test_translation_keyframes' recipe) keeps `trans_offset ≈ (0, 0, 0)` and `trans_scale ≈ (5/65535, 10/65535, 15/65535)`. Authoring at `[10, 0, 0]` shifts the on-disk TM by ways that depend on BoneSys internal conventions and break the assertion.
- **Dummy-with-objectoffsetrot rotation moves the position.** MaxScript's `node.rotation = ...` on a Dummy with `objectoffsetrot` set rotates AROUND the pivot, which translates the node origin if the node is positioned away from world origin. Authoring PivotedHardpoint at the world origin sidesteps this; the static offset alone tests the 5g×8b composition without confounding the translation track.

**Verifier groups (62 assertions across 10 groups):**

| Group | Assertions | Pins |
|---|---|---|
| A — Structural | A1-A6 | `n_frames=31`, `fps=30`, FoC mini-chunks present, no unknown leaf IDs inside `0x1002`, `0x100a` precedes `0x1009`, `n_scale_words=0` |
| B — Skeleton | B7-B13 | 11 bones declared, all expected names present, parent links correct, no extra bones |
| C — Pool sizes + slot assignment (bone_map enforcement) | C14-C21 | `n_rotation_words=16`, `n_translation_words=12`, each of the 4 animated bones has rot/trans slot indices, every other bone has `idx_rotation=-1` AND `idx_translation=-1` (catches "broadened bone_map" regressions) |
| D — Rotation correctness | D22-D29 | BoneA frame[30] relative = 90° Z, HelperBone = 45° X, PivotedHardpoint frame[0] = `q_offset`, frame[30] = `q_offset × q_authored_30` (5g×8b composition), all quats unit-length |
| E — Sign canonicalisation | E30-E32 | per-frame quat hemisphere continuity on all 3 rotation-animated bones |
| F — Translation correctness | F33-F38 | BoneB `trans_scale[axis] = delta/65535` per axis, frame[30]-frame[0] decoded delta = (5,10,15), constant-position bones (BoneA, HelperBone, PivotedHardpoint) have `trans_scale = (0,0,0)` |
| G — Visibility | G39-G50 | OmniLight + StaticBox emit 0x1007 with visible-then-hidden monotone pattern; prox0 emits with hidden-then-visible (direction-symmetric); SpotLight + SpotLight.Target + prox1 elided |
| H — Skinning unaffected by animation | H51-H55 | SkinCyl vertex count > 0, weight sums = 1.0, all bone refs valid + in {BoneA, BoneB}, root connection exists |
| I — Chunk ordering | I56-I58 | Top-level `0x1000` × 1, `0x1001` first child, `0x1002` count = 11 |
| J — `.export.log` cross-check | J59-J63 | Line says "11 bone(s) / 16 rotation word(s) / 12 translation word(s) / 3 visibility track(s)" |

Output: 11 bones, 16 rotation words, 12 translation words, 0 scale words, 3 visibility tracks.

Pinned signals:
- 62/62 verifier assertions pass.
- **Hard merge gate: all 10 prior gold SHAs preserved post-rebuild** — 7d (`56FCEAF0…`), 8b .alo (`3791F274…`), 8b .ala post-8c stable (`19a79cbb…`), 8c .alo (`A0C333A2…`), 8c .ala (`749EBCE8…`), 5g×3 (`0919E2DF…` / `00B21377…` / `C9466AF6…`), 8d .alo (`f2f027ff…`), 8d .ala (`7fe1478a…`). Proves 8e is a true no-op for prior tests.
- 31/31 harness, no spurious `.ala` siblings on non-animated tests.
- 3× byte-identical determinism: `.alo` `dbc4f82bc885619d…`, `.ala` `99a74727996120cd…`.
- Non-regression: 4929/4929 chunk-tree · 2066/2066 .alo strict · 2863/2863 .ala typed-pipeline · 81/81 unit — all unchanged.

**Five negative tripwires** (all reverted post-demonstration):

- **A** — broaden `bone_map` post-walks via `for (kv : visibility_map) bone_map[kv.first] = kv.second;` → `n_rotation_words=40` instead of 16; light/proxy/static-mesh bones get unwanted rot/trans slot assignments. Fires #C14, #C15, and 7× #C21.
- **B** — drop `visibility_map[inode] = proxy.bone_index;` in walk_proxies → prox0's visibility animation never lands on visibility_map → walker third pass never samples it → no 0x1007 emitted. Fires #G45.
- **C** — swap `for (... : visibility_map)` to `bone_map` in walk_animation third pass → visibility surface narrows to bone_map members (none of which are visibility-animated in this scene) → all 3 expected 0x1007 chunks vanish. Fires #G39 + #G42 + #G45.
- **D** — replace `compose_with_object_offset(animatable[i], node_tm)` with bare `node_tm` in walk_animation per-frame loop → PivotedHardpoint's static -90° Y offset stops composing per-frame → on-disk quat at frame 0 = identity instead of `q_offset`. Fires #D27 + #D28 (the 5g×8b cross is what makes this tripwire valuable — it's invisible in 5g-only and 8b-only tests since those don't combine the two).
- **E** — invert visibility threshold `>= 0.5` → `<` → OmniLight + StaticBox + prox0 bit patterns invert; AND SpotLight + SpotLight.Target + prox1 get spurious 0x1007 chunks (their constant-visible `1.0` samples now read as hidden under the inverted threshold → `any_hidden=true` → emission). Fires 9 visibility assertions across G39/G42/G45/G48/G49/G50.

Each tripwire bites a distinct architectural boundary in the walker; together they prove the verifier's bone_map/visibility_map enforcement, 5g composition coverage, and threshold-direction coverage are all load-bearing (not redundant or vestigial).

After 8e: Phase 8 acceptance is locked in. Phase 8f (regression battery + visibility-propagation Utility-panel button) follows to close Phase 8.

### Phase 9 — Polish + v1 release

Export options dialog (frame range, format-version flag, log path), structured warnings/errors panel, logging to file, README polish. Tag `v1.0`. **Publish the built `.dle` as a GitHub Releases artifact** — this is how users get the plugin since CI cannot build it.

---

## Open format questions

Tracked at the bottom of [`docs/format-notes.md`](format-notes.md). Most Phase 4-relevant questions are now resolved; the canonical table is in `format-notes.md`. The remaining open items (as of Phase 9.1's close-out pass) are listed there with their current resolution state.

---

## Max SDK / MaxScript quirks (calibrated against 3ds Max 2026 SDK)

Non-obvious behaviours of the Max APIs surfaced during walker / harness-test development. Each item is paired with the file or phase that demonstrates the behaviour, so a future investigation can verify if Max's behaviour has changed across versions. Calibrated against `MAX_SDK_DIR=C:/Program Files/Autodesk/3ds Max 2026 SDK/maxsdk` (MSVC 14.50). Behaviour may differ in earlier or later Max versions.

### TM accessors

- **`IGameNode::GetLocalTM(t)` / `GetWorldTM(t)` / `GetObjectTM(t)` all OMIT the object offset.** "Affect Pivot Only" rotation (Max's `objectoffsetrot`) is not captured by any IGame TM accessor. To compose the full pivot orientation, pull components via the underlying `INode`: `inode->GetObjOffsetRot()` / `GetObjOffsetPos()` / `GetObjOffsetScale()`. See Phase 5g's `compose_with_object_offset` helper in [`max2alamo/src/scene_walker.cpp`](../max2alamo/src/scene_walker.cpp). Composition order is `offset_TM * node_TM` (row-vector convention; offset applied first to convert pivot-space to node-space, then node TM to convert to parent-space). Confirmed via the empirical probes that became `test_pivot_affect_only.ms` (Phase 5g).

- **`INode::IsNodeHidden()` is STATIC — no TimeValue overload.** For per-frame visibility, use `INode::GetVisibility(TimeValue t, Interval* valid = NULL)` ([`maxsdk/include/inode.h:933`](C:\Program%20Files\Autodesk\3ds%20Max%202026%20SDK\maxsdk\include\inode.h)). Returns a float in `[0, 1]`: `0.0` = invisible, `1.0` = opaque, `< 0` = off. Threshold at `≥ 0.5` for binary on/off encoding into the .ala 0x1007 chunk (planned for Phase 8d).

- **`BoneSys.createBone <from> <to>` direction args do NOT propagate to the node TM.** A bone created from `[0,0,0]` to `[20,0,0]` (visually pointing +X) has node TM = identity. The bone's procedural-display direction lives in bone-object internal data with no Max SDK accessor. Programmatic bone authoring should set rotation via `node.rotation = …` if the direction needs to round-trip into the exported `.alo` matrix.

- **BoneSys bones DOUBLE-APPLY `objectoffsetrot`.** Setting `b.objectoffsetrot = quat -90°-Z` on a `BoneSys.createBone` produces 180° in the resulting node TM (composes via Max-internal logic that's not visible from the SDK). Workaround: use `b.rotation = …` instead of Affect Pivot Only for BoneSys bones. Dummy/Point helpers do NOT have this quirk and ARE the canonical hardpoint authoring pattern. See Phase 5g caveat in [`tests/maxscript/test_pivot_affect_only.ms`](../tests/maxscript/test_pivot_affect_only.ms) preamble.

### MaxScript

- **`local` keyword can only appear inside a block**, not at the top level of a `.ms` file. Top-level assignments are implicit globals.

- **`anchor` is a reserved or conflicting name**; use `anchorBox` or similar in test scenes.

- **`animate on (...)` block syntax** is the standard for keyframe authoring. Inside the block, `at time T (expr)` sets a key at frame T. Adjacent keys at frame N and N+1 with the same controller produce a sharp 1-frame transition (no in-between sample). Phase 8d's blinking-light test will rely on this for deterministic visibility encoding.

- **`rootNode` (global) returns the Max scene graph's root** — used to set/read scene-level user properties. Phase 8's `Alamo_Anim_Start` / `Alamo_Anim_End` / `Alamo_Anim_Name` live here.

### Skinning

- **`IGameMesh::GetVertex(idx, /*objectSpace=*/true)` returns object-space coordinates** (not world-space). Vertices feed into the `.alo`'s `0x10007` stream verbatim. The bone matrix written to `0x206` is the node TM, post-Phase 5g composed with object offset. Runtime `world = bone_matrix * vertex` works because vertices were authored relative to the same frame the bone matrix represents.

- **`IGameSkin::GetBone()` only returns real Max bones** (or helpers-as-bones). Skin-influence INodes are a subset of `bone_map` keys. The skin-resolution code in `walk_node` only LOOKS UP keys it gets from the modifier — adding extra entries to `bone_map` (e.g. for synth bones) is structurally safe for skinning. Phase 8d nevertheless introduces a SEPARATE `visibility_map` to avoid expanding rotation/translation track emission (which IS keyed off `bone_map` iteration) onto light/proxy/static-mesh synth bones.

### `.ala` format

- **0x1007 visibility chunks appear in BOTH EaW and FoC** corpus files, despite `docs/format-notes.md`'s bracketing implying they're EaW-only. Confirmed via `alo_dump` of FoC file `AI_RANCOR_ATTACK_00.ALA` at offset 5761. The chunk lives inside the per-bone `0x1002` container, sibling of `0x1003`.

- **Scale tracks are absent from vanilla FoC content.** Corpus survey during Phase 8c found 0/1500 FoC `.ala` files have `nScaleWords > 0`. The format defines no chunk ID for a file-scope scale pool. Phase 8c/8d emit `n_scale_words = 0` and leave `idx_scale = -1` on every bone; the `AlaBoneTrack::scale_*` fields stay at struct defaults.

### Authoring conventions

- **The user authors hardpoints as either:** (1) Dummy/Point helper with `Alamo_Export_Transform = true` (Phase 5e helpers-as-bones) and the pivot oriented via Hierarchy → Affect Pivot Only; OR (2) HIDDEN BoneSys bone (Max's standard `IsNodeHidden` flag) with direction set via `b.rotation = …`. The user does NOT use `Alamo_Export_Geometry = false` on a mesh as a hardpoint marker — that flag causes the walker to skip the whole node ([`scene_walker.cpp:862-870`](../max2alamo/src/scene_walker.cpp)), so no bone is emitted at all.

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

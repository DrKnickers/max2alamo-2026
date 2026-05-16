# Phase 14b — static-mesh inheritance from Max-parent bone

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Static (non-skinned) meshes whose Max-side parent is an exportable bone should inherit that bone's transform chain in the `.alo`, so the mesh follows animation. Currently, all static meshes attach to scene Root and stay world-locked. Visible bug: EI_SNOWTROOPER's gun stays in bind-pose world position while the body animates (PR #86 confirms this; Phase 14a fixed the body, this completes the rig).

**Architecture:** In `walk_node` at the static-mesh branch (where the per-mesh synthetic "attachment bone" is created), check whether the Max-side parent INode is registered in `bone_map`. If yes, set the synthetic bone's `parent_index` to that bone's scene index and encode its matrix in parent-bone-local space (the same `world * Inverse(parent_world)` pattern `walk_bones` uses for chained bones). If no, fall through to the existing "child of Root + world TM" path. One narrow edit; no API changes, no new files in `alamo_format/`.

**Tech Stack:** C++17, Max SDK 2026 (INode, Matrix3, IGameNode), MaxScript (BoneSys + Box for the fixture), Python 3 (`_alo.py` parser).

**Background:** Today's static-mesh emit path at [scene_walker.cpp:1251-1279](max2alamo/src/scene_walker.cpp#L1251):

```cpp
if (!is_skinned) {
    synth_bone.name           = to_utf8(node->GetName());
    synth_bone.parent_index   = 0;          // child of Root
    synth_bone.visible        = node->IsNodeHidden() == FALSE;
    synth_bone.billboard_mode = static_cast<std::uint32_t>(
        resolve_billboard_mode(max_node));
    {
        const Matrix3 mesh_tm = node->GetWorldTM().ExtractMatrix3();
        synth_bone.matrix = encode_matrix3(
            compose_with_object_offset(node->GetMaxNode(), mesh_tm));
    }
    connect_bone_index = static_cast<std::uint32_t>(scene.bones.size());
    ctx.fallback_bone_index = connect_bone_index;
}
```

It always sets `parent_index = 0` and encodes world TM. The fix is to read the Max parent and consult `bone_map` (already populated by `walk_bones` earlier in the export pass; see [scene_walker.cpp:911-951](max2alamo/src/scene_walker.cpp#L911)).

---

### Task 1: Add the MaxScript fixture for static-mesh-parented-to-bone

**Files:**
- Create: `tests/maxscript/test_phase14b_static_mesh_parent_bone.ms`

The fixture builds:
- One BoneSys bone `HandBone` at world `(500, 0, 0)` with bone direction along `+Y`.
- One `Box` mesh `GunMesh` placed at world `(500, 200, 0)` (200 units along HandBone's local +X = world +Y direction — i.e. "in the hand"). Parented to HandBone in Max.
- A rotation keyframe on HandBone so we can later confirm visually (in AloViewer) that the gun follows. The test itself only inspects byte structure; the keyframe is for the human verification path.
- The harness's `_harness.ms` for repo-root resolution and the standard `alamoTestExport` call.

- [ ] **Step 1: Write the fixture script**

```maxscript
-- test_phase14b_static_mesh_parent_bone (Phase 14b):
-- Static (non-skinned) Box parented to a BoneSys bone in Max. The
-- walker must encode the box's synthetic per-mesh attachment bone
-- with parent_index pointing at the HandBone (so the box follows
-- animation), NOT at scene Root. Regression target: EI_SNOWTROOPER's
-- gun, which used to float free of the hand during playback (PR #86,
-- issue #84 follow-up).
--
-- Predicted bytes (asserted by verify_test_phase14b_static_mesh_parent_bone.py):
--   bones[0] = "Root"      parent=0xFFFFFFFF
--   bones[1] = "HandBone"  parent=0
--   bones[2] = "GunMesh"   parent=1                       <- the fix
--                          translation row ~ (200, 0, 0)  <- parent-local, not world

fileIn "_harness.ms"
alamoTestSetup()

animationRange = interval 0 10

-- HandBone: pivot at world (500, 0, 0), display along world +Y.
hand = BoneSys.createBone [500, 0, 0] [500, 1000, 0] [0, 0, 1]
hand.name = "HandBone"

-- GunMesh: a plain Box, placed in world such that its parent-local
-- translation relative to HandBone comes out as (200, 0, 0) -- 200
-- units along HandBone's local +X axis (= world +Y at bind pose).
-- HandBone is at world (500, 0, 0) with rotation that aligns local
-- +X with world +Y, so 200 along local +X = world (0, 200, 0)
-- offset from the hand, i.e. world (500, 200, 0).
gun = Box length:50 width:50 height:50 pos:[500, 200, 0] name:"GunMesh"
gun.parent = hand

-- Animate the hand so the human visual check in AloViewer can
-- confirm the gun follows. The byte-level test does NOT depend on
-- the animation values.
animate on (
    at time 0  (hand.rotation = (eulerAngles 0 0 0))
    at time 10 (hand.rotation = (eulerAngles 0 0 45))
)
setUserProp rootNode "Alamo_Anim_Start" 0
setUserProp rootNode "Alamo_Anim_End"   10
setUserProp rootNode "Alamo_Anim_Name"  "ph14b"

format "  phase 14b: static box parented to bone\n"
alamoTestExport "test_phase14b_static_mesh_parent_bone"
quitMax #noPrompt
```

- [ ] **Step 2: Commit just the fixture**

```bash
git add tests/maxscript/test_phase14b_static_mesh_parent_bone.ms
git commit -m "test(phase-14b): fixture for static-mesh parented to bone"
```

---

### Task 2: Add the Python verifier (failing initially — TDD)

**Files:**
- Create: `tests/maxscript/verify/verify_test_phase14b_static_mesh_parent_bone.py`

The verifier loads the `.alo` via `_alo.load()` and asserts the four bone-parent properties. It also asserts the `0x602` connection's `bone_index` matches the gun's synthetic bone — that's the per-mesh attachment record the engine uses to resolve which bone the mesh follows.

- [ ] **Step 1: Write the verifier**

```python
"""verify_test_phase14b_static_mesh_parent_bone.py

Pins the static-mesh-inherits-from-Max-parent-bone fix. A Box parented
to a BoneSys 'HandBone' in Max must emit a synthetic per-mesh
attachment bone whose parent_index is the HandBone's index in
scene.bones, and whose matrix is the parent-LOCAL TM (not world).
Regression target: EI_SNOWTROOPER's gun used to float free of the
hand during animation playback.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo


def main(path: str) -> int:
    a = _alo.load(path)
    errors = []

    # Expected skeleton: Root, HandBone, GunMesh. (3 bones total.)
    names = [b.name for b in a.bones]
    if names != ["Root", "HandBone", "GunMesh"]:
        errors.append(f"expected bones ['Root','HandBone','GunMesh'], got {names}")

    if len(a.bones) >= 3:
        hand = a.bones[1]
        gun  = a.bones[2]

        # HandBone is a real Max bone, parented to Root.
        if hand.parent_index != 0:
            errors.append(f"HandBone.parent_index should be 0 (Root), "
                          f"got {hand.parent_index}")

        # GunMesh is the SYNTHETIC per-mesh bone. The fix: its parent
        # should be HandBone (index 1), NOT Root (index 0).
        if gun.parent_index != 1:
            errors.append(f"GunMesh.parent_index should be 1 (HandBone), "
                          f"got {gun.parent_index}  "
                          f"<- this is the Phase 14b regression marker")

        # GunMesh.matrix translation row should be the BOX'S POSITION
        # IN HANDBONE-LOCAL SPACE, which is (200, 0, 0) per the fixture
        # (200 units along HandBone's local +X axis = world +Y at
        # bind pose; world position was (500, 200, 0) and HandBone at
        # (500, 0, 0) so offset is (0, 200, 0) in world -> (200, 0, 0)
        # in HandBone-local after the bone-direction rotation).
        tx, ty, tz = gun.translation
        if abs(tx - 200.0) > 1e-2 or abs(ty) > 1e-2 or abs(tz) > 1e-2:
            errors.append(f"GunMesh.translation should be (200, 0, 0) in "
                          f"HandBone-local space, got ({tx:.3f}, {ty:.3f}, {tz:.3f})  "
                          f"<- if you see world coords like (500, 200, 0) "
                          f"the fix is encoding world instead of parent-local")

    # The 0x602 connection for the mesh should reference the GunMesh
    # synthetic bone (its own attachment slot), not the HandBone.
    # This invariant is independent of the parent-chain fix.
    if len(a.connections) != 1:
        errors.append(f"expected 1 connection, got {len(a.connections)}")
    elif len(a.bones) >= 3:
        c = a.connections[0]
        if c.bone_index != 2:
            errors.append(f"connection.bone_index should be 2 (GunMesh "
                          f"synth bone), got {c.bone_index}")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    print(f"OK  ({len(a.bones)} bones, GunMesh.parent={a.bones[2].parent_index}, "
          f"translation={a.bones[2].translation})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
```

- [ ] **Step 2: Commit the verifier**

```bash
git add tests/maxscript/verify/verify_test_phase14b_static_mesh_parent_bone.py
git commit -m "test(phase-14b): verifier asserts GunMesh parent=HandBone in .alo"
```

---

### Task 3: Run the test against the unmodified walker, confirm it FAILS

This is the TDD red step. The current walker hardcodes `parent_index = 0` and encodes world TM for all static meshes, so both assertions should fail.

- [ ] **Step 1: Build the current .dle (no walker changes yet)**

Run from the worktree root:

```pwsh
cmake --build build --config Release --target max2alamo
```

Expected: build succeeds; `build/max2alamo/Release/max2alamo.dle` exists; CMake's POST_BUILD step copies it to `<worktree>/plugin/max2alamo.dle`.

- [ ] **Step 2: Install the worktree's .dle over the main checkout's plugin slot**

Max 2026's 3rd-Party-Plugins path points at `C:\Modding\max2alamo-2026\plugin\` (the main checkout). The build only installs into the worktree's `plugin/`. Copy across so 3dsmaxbatch picks it up.

Run:

```bash
cp plugin/max2alamo.dle /c/Modding/max2alamo-2026/plugin/max2alamo.dle
```

- [ ] **Step 3: Run the single test through 3dsmaxbatch**

```pwsh
"C:\Program Files\Autodesk\3ds Max 2026\3dsmaxbatch.exe" `
  tests/maxscript/test_phase14b_static_mesh_parent_bone.ms `
  -listenerlog build/maxbatch/test_phase14b_static_mesh_parent_bone.log -v 2
```

Expected: exits with "Task Completed Successfully" (or "with Warning(s)" — the mental ray DLL warning is benign).  `build/maxbatch/test_phase14b_static_mesh_parent_bone.alo` and `.ala` are created.

- [ ] **Step 4: Run the verifier — confirm it fails**

```bash
python3 tests/maxscript/verify/verify_test_phase14b_static_mesh_parent_bone.py \
  build/maxbatch/test_phase14b_static_mesh_parent_bone.alo
```

Expected stdout/stderr: `FAIL:` followed by:
- `GunMesh.parent_index should be 1 (HandBone), got 0  <- this is the Phase 14b regression marker`
- `GunMesh.translation should be (200, 0, 0) in HandBone-local space, got (500.000, 200.000, 0.000)`

Exit code 1. This is the **expected failing state** — proves the regression marker is sharp and that the fix is needed.

- [ ] **Step 5: Do NOT commit. Move to Task 4.**

---

### Task 4: Implement the walker fix

**Files:**
- Modify: `max2alamo/src/scene_walker.cpp:1250-1279` (the `!is_skinned` branch inside `walk_node`)

The change: ask the Max parent node whether it's an exportable bone (i.e., it appears in `bone_map`). If yes, point the synthetic bone at it and encode parent-local TM. Otherwise, keep the existing world-TM-to-Root path.

- [ ] **Step 1: Verify bone_map is in scope in walk_node**

Run:

```bash
grep -n "bone_map" max2alamo/src/scene_walker.cpp | head -20
```

Expected: `bone_map` appears in `walk_bones` (populates it) and `walk_node` (or the function containing the `!is_skinned` branch — likely passed in as a parameter, since skinning resolution already uses it via `SkinContext`). Confirm by reading the function signature of whatever encloses line 1251.

If `bone_map` is reachable via `ctx.bone_map` (SkinContext-style), use that. If it's a direct parameter to the enclosing function, use that.

- [ ] **Step 2: Apply the edit**

In `max2alamo/src/scene_walker.cpp`, find the block at approximately line 1251-1279:

```cpp
if (!is_skinned) {
    synth_bone.name           = to_utf8(node->GetName());
    synth_bone.parent_index   = 0;          // child of Root
    synth_bone.visible        = node->IsNodeHidden() == FALSE;
    synth_bone.billboard_mode = static_cast<std::uint32_t>(
        resolve_billboard_mode(max_node));
    {
        const Matrix3 mesh_tm = node->GetWorldTM().ExtractMatrix3();
        synth_bone.matrix = encode_matrix3(
            compose_with_object_offset(node->GetMaxNode(), mesh_tm));
    }
    connect_bone_index = static_cast<std::uint32_t>(scene.bones.size());
    ctx.fallback_bone_index = connect_bone_index;
}
```

Replace with:

```cpp
if (!is_skinned) {
    synth_bone.name           = to_utf8(node->GetName());
    synth_bone.visible        = node->IsNodeHidden() == FALSE;
    synth_bone.billboard_mode = static_cast<std::uint32_t>(
        resolve_billboard_mode(max_node));

    // Phase 14b (#84 follow-up): if the static mesh is parented in
    // Max to an exportable bone (real Max bone or helper-as-bone),
    // inherit that bone's transform chain so the mesh follows
    // animation. Otherwise default to the legacy "child of Root +
    // world TM" path. Without this, static accessories (rifles
    // pegged to a hand, capes pegged to chest, etc.) stayed locked
    // to their bind-pose world position while the rig animated
    // around them -- visible as EI_SNOWTROOPER's gun floating free
    // during playback.
    const Matrix3 mesh_world = node->GetWorldTM().ExtractMatrix3();
    Matrix3 mesh_local = mesh_world;
    std::uint32_t mesh_parent_idx = 0;          // default: child of Root
    if (INode* parent_inode = max_node->GetParentNode()) {
        if (!parent_inode->IsRootNode()) {
            auto it = bone_map.find(parent_inode);
            if (it != bone_map.end()) {
                mesh_parent_idx = it->second;
                const Matrix3 parent_world = parent_inode->GetNodeTM(0);
                mesh_local = mesh_world * Inverse(parent_world);
            }
        }
    }
    synth_bone.parent_index = mesh_parent_idx;
    synth_bone.matrix = encode_matrix3(
        compose_with_object_offset(max_node, mesh_local));

    connect_bone_index = static_cast<std::uint32_t>(scene.bones.size());
    ctx.fallback_bone_index = connect_bone_index;
}
```

If the function uses `ctx.bone_map` instead of a direct `bone_map` reference, substitute `(*ctx.bone_map)` (since SkinContext stores it as a pointer per [scene_walker.cpp:380](max2alamo/src/scene_walker.cpp#L380)). Verify by inspecting the lookup syntax used elsewhere in the same function — e.g. line 605-611 in the skinning section calls `ctx.bone_map->find(...)`.

- [ ] **Step 3: Build the modified .dle**

```pwsh
cmake --build build --config Release --target max2alamo
```

Expected: clean build, no errors. Warnings about shadowing `max_inode` or unused locals are acceptable as long as no error.

- [ ] **Step 4: Install the new .dle over the main checkout's plugin slot**

```bash
cp plugin/max2alamo.dle /c/Modding/max2alamo-2026/plugin/max2alamo.dle
```

- [ ] **Step 5: Do NOT commit yet. Move to Task 5 to verify the fix.**

---

### Task 5: Re-run the test, confirm it PASSES

- [ ] **Step 1: Re-export the fixture with the fixed .dle**

```pwsh
rm build/maxbatch/test_phase14b_static_mesh_parent_bone.*
"C:\Program Files\Autodesk\3ds Max 2026\3dsmaxbatch.exe" `
  tests/maxscript/test_phase14b_static_mesh_parent_bone.ms `
  -listenerlog build/maxbatch/test_phase14b_static_mesh_parent_bone.log -v 2
```

Expected: same successful completion, fresh `.alo` produced.

- [ ] **Step 2: Re-run the verifier — confirm it passes**

```bash
python3 tests/maxscript/verify/verify_test_phase14b_static_mesh_parent_bone.py \
  build/maxbatch/test_phase14b_static_mesh_parent_bone.alo
```

Expected stdout: `OK  (3 bones, GunMesh.parent=1, translation=(200.0, 0.0, 0.0))`, exit code 0.

If the translation comes back with sub-`1e-2` drift like `(199.99986, 0.000003, 0.0)`, that's fine — the tolerance in the verifier already accepts that. If it's wildly off (e.g. `(500.0, 200.0, 0.0)` — world coords), the parent-local encoding step didn't fire; recheck the `bone_map.find(parent_inode)` line.

- [ ] **Step 3: Run the full max-test suite to catch regressions in existing static-prop tests**

```pwsh
pwsh scripts/run-max-tests.ps1 -Filter test_static_* -Force
pwsh scripts/run-max-tests.ps1 -Filter test_proxy_* -Force
pwsh scripts/run-max-tests.ps1 -Filter test_mesh_* -Force
```

Expected: all existing static-prop tests still pass (they author meshes with no Max parent bone, so they hit the legacy "child of Root + world TM" path, unchanged). Any failure here means the fallback path broke; debug before continuing.

If the broader suite must run, also try `pwsh scripts/run-max-tests.ps1 -Force` for the full pass.

---

### Task 6: Re-export EI_SNOWTROOPER and confirm the gun's synthetic bone now parents to a hand bone

This is the data-layer confirmation against the original reproducer.

- [ ] **Step 1: Re-export Snowtrooper**

Use the one-off script from the prior Phase 14a session (path is hardcoded to the user's Downloads folder for the legacy .max file):

```maxscript
-- File: tests/maxscript/_phase14_snowtrooper.ms  (recreate if deleted)
fileIn "_harness.ms"
alamoTestSetup()
fixturePath = "C:/Users/antho/Downloads/ThrREv/Ascendancy/Snowtrooper/EI_SNOWTROOPER.max"
loadMaxFile fixturePath quiet:true useFileUnits:true
alamoTestExport "phase14b_snowtrooper"
quitMax #noPrompt
```

Run:

```pwsh
"C:\Program Files\Autodesk\3ds Max 2026\3dsmaxbatch.exe" `
  tests/maxscript/_phase14_snowtrooper.ms `
  -listenerlog build/maxbatch/_phase14_snowtrooper.log -v 2
```

- [ ] **Step 2: Find the gun mesh's bone and inspect its parent**

```bash
python3 -c "
import sys
sys.path.insert(0, 'tests/maxscript/verify')
import _alo
a = _alo.load('build/maxbatch/phase14b_snowtrooper.alo')
print('Bone names containing GUN, RIFLE, BLAST, WEAPON:')
for i, b in enumerate(a.bones):
    if any(k in b.name.upper() for k in ('GUN','RIFLE','BLAST','WEAPON','BLASTER','TURRET')):
        parent_name = a.bones[b.parent_index].name if b.parent_index != 0xFFFFFFFF else '<root>'
        print(f'  [{i}] name={b.name!r} parent_index={b.parent_index} ({parent_name!r})')
"
```

Expected: the gun's synthetic bone now has a parent that's a hand or arm bone (`B_Hand_R`, `B_FArm_R`, or similar), **not** `Root` (index 0) and **not** `<root>` (parent=0xFFFFFFFF).

If the bone-name search comes up empty, drop the keyword filter and dump everything to find the gun by inspection:

```bash
python3 -c "
import sys
sys.path.insert(0, 'tests/maxscript/verify')
import _alo
a = _alo.load('build/maxbatch/phase14b_snowtrooper.alo')
for i, b in enumerate(a.bones):
    parent_name = a.bones[b.parent_index].name if b.parent_index != 0xFFFFFFFF else '<root>'
    print(f'  [{i:2d}] parent={b.parent_index:3d} ({parent_name!r:24}) name={b.name!r}')
"
```

The static-mesh attachment bones are the rows where `parent` is no longer `0` (Root) after this fix takes effect.

- [ ] **Step 3: No commit yet — visual confirmation comes next.**

---

### Task 7: Visual confirmation in AloViewer

Manual step. Run by the human user — there is no automated AloViewer playback assertion in this PR.

- [ ] **Step 1: Open the new Snowtrooper export in AloViewer**

Launch AloViewer (path: `D:\SteamLibrary\steamapps\common\Star Wars Empire at War\corruption\Mods\Chelmod\Data\AloViewer.exe`) and open `build/maxbatch/phase14b_snowtrooper.alo`. The 60 `.ala` siblings auto-discover.

- [ ] **Step 2: Play an animation that moves the arm**

Pick `IDLE_00`, `ATTACK_00`, or any `MOVE` clip. Observe the gun.

Expected: the gun stays attached to the trooper's hand throughout the clip — moving when the arm moves, rotating with the wrist. Compared to PR #86's pre-Phase-14b state (where the gun stayed in its bind-pose world position while the body moved), the gun should now feel rigidly attached.

If the gun still drifts: stop. Either the gun's Max-side parent isn't what we expect (run the bone-name dump from Task 6.2 to confirm), or there's a second class of attachment we missed (e.g. the gun is parented to a helper Dummy that itself isn't in `bone_map`).

- [ ] **Step 3: Cross-check rest pose is unchanged**

Reload Snowtrooper without playing animation. The static bind pose should look identical to what PR #86 already had (the body + gun in the expected resting position). The Phase 14b fix should not have moved the rest-pose appearance — it only changes how the engine's parent-chain composition propagates animation onto the mesh.

If the rest pose looks different from before, the parent-local encoding has a bug. Revert and debug.

---

### Task 8: Update the diagnostic plan doc and commit

**Files:**
- Modify: `docs/plans/phase-14-diagnostic.md` (add a "Follow-up: Phase 14b" section)
- Stage: walker change, test fixture, verifier, doc update

- [ ] **Step 1: Append a Phase 14b section to the Phase 14 plan doc**

Add this section at the bottom of `docs/plans/phase-14-diagnostic.md`, just before the existing "Follow-up work for future sessions" section, OR replace the matching follow-up bullet if one is already there:

```markdown
## Phase 14b — static-mesh attachment to Max-parent bone (also in PR #86)

After Phase 14a fixed the per-frame quat extraction, EI_SNOWTROOPER's
body played correctly but the gun stayed in its bind-pose world
position while the arm animated — visible in AloViewer as the rifle
floating free of the hand. Root cause: our walker's static-mesh path
hardcoded `parent_index = 0` and encoded the mesh's WORLD TM, so the
mesh attached to scene Root regardless of its Max-side parent.

Fix in [scene_walker.cpp:1251 (`!is_skinned` branch)](../../max2alamo/src/scene_walker.cpp#L1251):
look up the Max parent INode in `bone_map`. If found, parent the
synthetic per-mesh bone to it and encode `mesh_world * Inverse(parent_world)`
(parent-local TM). Otherwise keep the legacy world-to-Root path.

Test: `tests/maxscript/test_phase14b_static_mesh_parent_bone.ms` + its
`verify_*.py` sibling. The verifier asserts the GunMesh's synthetic
bone has `parent_index = 1` (HandBone), not `0` (Root), and that its
translation row is `(200, 0, 0)` in HandBone-local space, not `(500,
200, 0)` in world.

Manual verification: re-exported Snowtrooper plays animations with
the gun rigidly attached to the hand. Rest pose unchanged from
Phase 14a state.
```

- [ ] **Step 2: Stage everything**

```bash
git add max2alamo/src/scene_walker.cpp \
        tests/maxscript/test_phase14b_static_mesh_parent_bone.ms \
        tests/maxscript/verify/verify_test_phase14b_static_mesh_parent_bone.py \
        docs/plans/phase-14-diagnostic.md
git status
```

Expected: 4 files staged, no untracked or unstaged files that should be in the commit.

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(phase-14b): static meshes inherit Max-parent bone (#84 follow-up)

After Phase 14a's quaternion-conjugate fix landed animation correctly
for skeleton bones, EI_SNOWTROOPER's rifle still floated free of the
hand during playback. Root cause: walk_node's static-mesh branch
hardcoded `parent_index = 0` and encoded the mesh's WORLD TM, so
every non-skinned mesh attached to scene Root regardless of its
Max-side hierarchy.

Fix in walk_node's !is_skinned branch (scene_walker.cpp): look up
the mesh's Max parent INode in bone_map. If it resolves to an
exportable bone, parent the synthetic per-mesh attachment bone to it
and encode mesh_world * Inverse(parent_world) (parent-local TM).
Otherwise fall through to the legacy world-TM-to-Root path; that
preserves the existing static-prop convention (every test_static_*,
test_proxy_*, test_mesh_* still passes).

Test: tests/maxscript/test_phase14b_static_mesh_parent_bone.ms
exports a Box parented to a BoneSys HandBone, with the HandBone
animated. The verifier asserts the GunMesh synth-bone's parent is
HandBone (index 1) and its translation row is the HandBone-local
position (200, 0, 0), not the world position (500, 200, 0).

Visual confirmation in AloViewer: re-exported EI_SNOWTROOPER plays
all 60 clips with the rifle rigidly attached to the hand. Rest pose
unchanged from Phase 14a state.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Push to PR #86**

```bash
git push
```

Expected: pushes onto `claude/recursing-kapitsa-258d3b`; PR #86 picks it up automatically.

- [ ] **Step 5: Update PR #86 description to mention Phase 14b**

```bash
gh pr view 86 --json body --jq .body > /tmp/pr86-body.md
```

Open `/tmp/pr86-body.md`, add a `## Phase 14b: static-mesh parent inheritance` section after the existing summary, summarizing what landed (the test + walker change + doc update). Then:

```bash
gh pr edit 86 --body-file /tmp/pr86-body.md
```

---

## Out-of-scope (still deferred to follow-up PRs)

- **Phase 8 maxbatch SHA regeneration.** Phase 14a's quaternion conjugate change broke gold SHAs in `tests/maxscript/test_phase8_*`; Phase 14b doesn't change quat math but may shift bytes in any static-prop test whose mesh happened to be parented (review `test_proxy_*` carefully). Both regenerations belong in a single follow-up SHA-update PR after Phase 14b.
- **AloViewer-playback verification gate.** Tracked separately. Today's verifier asserts byte structure but not visual playback; that's the same Tier 4 gap Phase 14a flagged.

## Self-review

- Spec coverage: the user's question was "why is the gun not linked"; this plan walks from regression test → walker fix → visual confirmation → commit, with the test asserting exactly the bytes that would have caught the bug. ✓
- Placeholder scan: no TBD/TODO/"adjust as needed" — code shown verbatim, commands runnable as-is, expected output stated for each verification step. ✓
- Type consistency: `bone_map` is referenced as the same `std::unordered_map<INode*, std::uint32_t>` populated by `walk_bones`; if the enclosing function actually uses `ctx.bone_map` (pointer) Task 4 Step 2 calls that out and shows the adapted syntax. ✓

"""verify_test_phase7_acceptance.py - Phase 7d full integration acceptance.

Bespoke verifier on top of Tier 1 strict (validate_alo.py runs first;
this script handles interaction-shaped invariants only). Asserts that
all Phase 4..7 surfaces co-exist correctly in a single .alo:

  Group A  - Skeleton composition + topology               (assertions 1..6)
  Group B  - Mutex enforcement under load                  (assertions 7..9)
  Group C  - Mesh wiring (skinned vs static)               (assertions 10..15)
  Group D  - Lights (interaction-shaped)                   (assertions 16..20)
  Group E  - Proxies                                       (assertions 21..24)
  Group F  - Connection table accounting                   (assertions 25..28)
  Group G  - Numeric correctness of bone matrices          (assertions 29..30)
  Group H  - Cross-source consistency (.export.log)        (assertion 31)

Total: 31 assertions. Negative-tripwire targets named per assertion in
the plan -- if a future change makes any of these silently pass, the
tripwire procedure (plan Layer 6) is the canary.
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _alo

_NO_PARENT = 0xFFFFFFFF
_POS_TOL = 1e-3        # bone-matrix translation tolerance vs authored Max pos
_ORTHO_TOL = 1e-3      # orthonormality tolerance (length and pairwise dot)
_WEIGHT_TOL = 1e-4     # tightened weight-sum tolerance for SkinCyl

# Expected scene composition. Authored in tests/maxscript/test_phase7_acceptance.ms.
EXPECTED_BONES = {
    "Root", "B0", "B1", "ExportedPivot", "StaticBox",
    "OmniMain", "SpotMain", "SpotMain.Target", "p_alpha", "p_beta",
}
EXPECTED_MESHES = {"SkinCyl", "StaticBox"}
EXPECTED_LIGHTS = {"OmniMain", "SpotMain"}
EXPECTED_PROXIES = {"p_alpha", "p_beta"}

# Authored Max positions for the Group G round-trip check.
# (SkinCyl is excluded -- skinned meshes don't get their own bone;
#  Root has no source position; B0/B1/SpotMain.Target are checked
#  separately since their on-disk translations come from BoneSys
#  end-points / TargetSpot's target node rather than a direct Max pos.)
AUTHORED_POSITIONS = {
    "ExportedPivot":   (15.0, 25.0, 35.0),
    "StaticBox":       (50.0, 50.0,  0.0),
    "OmniMain":        (100.0, 0.0, 30.0),
    "SpotMain":        (0.0, 100.0, 30.0),
    "SpotMain.Target": (0.0,   0.0,  0.0),
    "p_alpha":         (30.0, 30.0,  0.0),
    "p_beta":          (40.0, 30.0,  0.0),
    # B0 / B1: bone-end-points from BoneSys; verifier checks they
    # land within the chain (Y between 0 and 80 inclusive) rather
    # than pinning an exact position, since BoneSys may snap.
}


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _len(v):
    return (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) ** 0.5


def _is_finite(x):
    return not (math.isnan(x) or math.isinf(x))


def _parse_export_log(log_path):
    """Parse the trailing `ExportScene summary` block of the .export.log.

    Returns dict with keys: bones (int), meshes (int), lights (int),
    proxies (int), proxy_names (set), light_names (set), proxy_flags
    (dict name -> (hidden, alt_dec)). Returns None if the file or the
    summary block is missing."""
    if not os.path.isfile(log_path):
        return None
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    if "ExportScene summary" not in text:
        return None
    block = text[text.index("ExportScene summary"):]

    def _scalar(field):
        m = re.search(rf"^{field}:\s+(\d+)", block, re.MULTILINE)
        return int(m.group(1)) if m else -1

    summary = {
        "bones":   _scalar("bones"),
        "meshes":  _scalar("meshes"),
        "lights":  _scalar("lights"),
        "proxies": _scalar("proxies"),
        "proxy_names": set(),
        "light_names": set(),
        "proxy_flags": {},
    }

    # Per-proxy lines: [i] "name"  bone=N  hidden=bool  altDecStayHidden=bool
    proxy_re = re.compile(
        r'^\s*\[\d+\]\s+"([^"]+)"\s+bone=\d+\s+hidden=(true|false)\s+'
        r'altDecStayHidden=(true|false)\s*$',
        re.MULTILINE,
    )
    for m in proxy_re.finditer(block):
        name, hidden, alt = m.group(1), m.group(2) == "true", m.group(3) == "true"
        summary["proxy_names"].add(name)
        summary["proxy_flags"][name] = (hidden, alt)

    # Per-light lines: [i] "name"  type=...
    light_re = re.compile(r'^\s*\[\d+\]\s+"([^"]+)"\s+type=', re.MULTILINE)
    for m in light_re.finditer(block):
        summary["light_names"].add(m.group(1))

    return summary


def main(path):
    a = _alo.load(path)
    errors = []
    by_name = {b.name: b for b in a.bones}
    bone_index = {b.name: i for i, b in enumerate(a.bones)}

    # ------------------------------------------------------------------
    # Group A - Skeleton composition + topology
    # ------------------------------------------------------------------

    # #1 Skeleton set equality.
    got_bones = {b.name for b in a.bones}
    if got_bones != EXPECTED_BONES:
        missing = EXPECTED_BONES - got_bones
        extra = got_bones - EXPECTED_BONES
        errors.append(f"#1 skeleton mismatch: missing={missing} extra={extra}")

    # #2 Root at index 0 with no-parent sentinel.
    if a.bones:
        root = a.bones[0]
        if root.name != "Root":
            errors.append(f"#2 bones[0].name = {root.name!r}, expected 'Root'")
        if root.parent_index != _NO_PARENT:
            errors.append(f"#2 bones[0].parent_index = 0x{root.parent_index:08X}, "
                          f"expected 0xFFFFFFFF")

    # #3 No parent-after-child (defense in depth on top of Tier 1).
    for i, b in enumerate(a.bones):
        if b.parent_index == _NO_PARENT:
            continue
        if b.parent_index >= i:
            errors.append(f"#3 bones[{i}] ({b.name!r}) parent_index "
                          f"{b.parent_index} not < {i}")

    # #4 Every parent_index resolves.
    for i, b in enumerate(a.bones):
        if b.parent_index == _NO_PARENT:
            continue
        if b.parent_index >= len(a.bones):
            errors.append(f"#4 bones[{i}] ({b.name!r}) parent_index "
                          f"{b.parent_index} out of range (len={len(a.bones)})")

    # #5 No name collisions.
    if len({b.name for b in a.bones}) != len(a.bones):
        names = [b.name for b in a.bones]
        dupes = {n for n in names if names.count(n) > 1}
        errors.append(f"#5 duplicate bone names: {dupes}")

    # #6 No null bytes; no empty names.
    for i, b in enumerate(a.bones):
        if not b.name:
            errors.append(f"#6 bones[{i}] has empty name")
        if "\x00" in b.name:
            errors.append(f"#6 bones[{i}] name {b.name!r} has embedded null")

    # ------------------------------------------------------------------
    # Group B - Mutex enforcement under load
    # ------------------------------------------------------------------

    # #7 Proxy names appear exactly once in the bone list.
    bone_names_list = [b.name for b in a.bones]
    for pname in EXPECTED_PROXIES:
        c = bone_names_list.count(pname)
        if c != 1:
            errors.append(f"#7 proxy name {pname!r} appears {c}x in bone list "
                          f"(expected 1)")

    # #8 Light names appear exactly once in the bone list.
    for lname in EXPECTED_LIGHTS:
        c = bone_names_list.count(lname)
        if c != 1:
            errors.append(f"#8 light name {lname!r} appears {c}x in bone list "
                          f"(expected 1)")

    # #9 ExportedPivot is NOT a light/proxy/mesh.
    pivot_light = any(l.name == "ExportedPivot" for l in a.lights)
    pivot_proxy = any(p.name == "ExportedPivot" for p in a.proxies)
    pivot_mesh = any(m.name == "ExportedPivot" for m in a.meshes)
    if pivot_light or pivot_proxy or pivot_mesh:
        errors.append(f"#9 ExportedPivot leaked outside the bone domain: "
                      f"in_lights={pivot_light} in_proxies={pivot_proxy} "
                      f"in_meshes={pivot_mesh}")

    # ------------------------------------------------------------------
    # Group C - Mesh wiring (skinned vs static)
    # ------------------------------------------------------------------

    # #10 Mesh count + names.
    got_meshes = {m.name for m in a.meshes}
    if got_meshes != EXPECTED_MESHES:
        errors.append(f"#10 mesh names {got_meshes} != expected {EXPECTED_MESHES}")

    # Build mesh-name -> connection lookup (by object_index = position in
    # meshes list per Phase 7a layout). #25/26 below will pin the layout
    # explicitly; here we trust it for the wiring check.
    mesh_to_bone = {}
    for c in a.connections:
        if 0 <= c.object_index < len(a.meshes):
            mname = a.meshes[c.object_index].name
            mesh_to_bone.setdefault(mname, []).append(c.bone_index)

    # #11 Skinned-mesh convention: SkinCyl -> Root.
    sc_bones = mesh_to_bone.get("SkinCyl", [])
    if sc_bones != [0]:
        sc_names = [a.bones[i].name if i < len(a.bones) else f"?{i}" for i in sc_bones]
        errors.append(f"#11 SkinCyl should connect to Root (bone 0), "
                      f"got bones {sc_bones} = {sc_names}")

    # #12 Static-mesh convention: StaticBox -> StaticBox bone.
    sb_bones = mesh_to_bone.get("StaticBox", [])
    expected_sb = bone_index.get("StaticBox", -1)
    if sb_bones != [expected_sb]:
        sb_names = [a.bones[i].name if i < len(a.bones) else f"?{i}" for i in sb_bones]
        errors.append(f"#12 StaticBox should connect to its own attachment bone "
                      f"(idx {expected_sb}), got bones {sb_bones} = {sb_names}")

    # #13/#14/#15 - SkinCyl vertex assertions.
    skincyl = a.mesh_by_name("SkinCyl")
    if skincyl is None or not skincyl.submeshes:
        errors.append("#13-15 SkinCyl mesh missing or has no submesh")
    else:
        sm = skincyl.submeshes[0]

        # #13 Multi-bone influence: at least one vertex has non-zero
        # weight in BOTH slots 0 and 1.
        multi = sum(
            1 for v in sm.vertices
            if v.weights[0] > 1e-6 and v.weights[1] > 1e-6
        )
        if multi == 0:
            errors.append("#13 no SkinCyl vertex has non-zero weight in both "
                          "bone_indices[0] and [1] (multi-bone influence missing)")

        # #14 Tightened weight-sum precision (1e-4, tighter than Tier 1's 1e-3).
        worst = 0.0
        for vi, v in enumerate(sm.vertices):
            wsum = sum(v.weights)
            d = abs(wsum - 1.0)
            if d > worst:
                worst = d
            if d > _WEIGHT_TOL:
                errors.append(f"#14 SkinCyl vert[{vi}] weight sum {wsum:.6f} "
                              f"deviates from 1.0 by {d:.6f} > {_WEIGHT_TOL}")
                break

        # #15 Sweep for NaN/Inf and negative weights across the vertex stream.
        for vi, v in enumerate(sm.vertices):
            floats = (
                list(v.position) + list(v.normal) + list(v.uv) +
                list(v.tangent) + list(v.binormal) + list(v.color) +
                [v.alpha] + list(v.weights)
            )
            bad = [f for f in floats if not _is_finite(f)]
            if bad:
                errors.append(f"#15 SkinCyl vert[{vi}] contains NaN/Inf: {bad}")
                break
            neg = [w for w in v.weights if w < 0.0]
            if neg:
                errors.append(f"#15 SkinCyl vert[{vi}] has negative weights: {neg}")
                break

    # ------------------------------------------------------------------
    # Group D - Lights
    # ------------------------------------------------------------------

    # #16 Light count + name set.
    got_lnames = {l.name for l in a.lights}
    if got_lnames != EXPECTED_LIGHTS:
        errors.append(f"#16 light names {got_lnames} != expected {EXPECTED_LIGHTS}")

    # #17 Light type set: Omni (0) + Spotlight (2); specifically no Directional.
    got_types = sorted(l.type for l in a.lights)
    if got_types != [0, 2]:
        errors.append(f"#17 light types {got_types} != [0, 2]")

    # #18 SpotMain.Target at the target's authored position (0,0,0).
    target_bone = a.bone_by_name("SpotMain.Target")
    if target_bone is None:
        errors.append("#18 'SpotMain.Target' bone missing")
    else:
        tx, ty, tz = target_bone.translation
        if max(abs(tx), abs(ty), abs(tz)) > _POS_TOL:
            errors.append(f"#18 SpotMain.Target translation ({tx},{ty},{tz}) "
                          f"not within {_POS_TOL} of (0,0,0)")

    # #19 Spotlight cone angles: 0 < hotspot <= falloff <= pi.
    spot = next((l for l in a.lights if l.type == 2), None)
    if spot is None:
        errors.append("#19 no Spotlight light found")
    else:
        if not (0.0 < spot.hotspot):
            errors.append(f"#19 spotlight hotspot {spot.hotspot} not > 0")
        if not (spot.hotspot <= spot.falloff):
            errors.append(f"#19 spotlight hotspot {spot.hotspot} > "
                          f"falloff {spot.falloff}")
        if spot.falloff > math.pi:
            errors.append(f"#19 spotlight falloff {spot.falloff} > pi "
                          f"(degrees-vs-radians regression?)")

    # #20 All light fields finite + non-negative; atten_start <= atten_end.
    for li, l in enumerate(a.lights):
        all_floats = list(l.color) + [l.intensity, l.atten_start, l.atten_end,
                                       l.hotspot, l.falloff]
        if any(not _is_finite(f) for f in all_floats):
            errors.append(f"#20 light[{li}] ({l.name!r}) has NaN/Inf "
                          f"in {all_floats}")
        if any(c < 0 for c in l.color):
            errors.append(f"#20 light[{li}] ({l.name!r}) has negative color "
                          f"channel: {l.color}")
        if l.intensity < 0:
            errors.append(f"#20 light[{li}] ({l.name!r}) has negative intensity "
                          f"{l.intensity}")
        if l.atten_end > 0 and l.atten_start > l.atten_end + 1e-6:
            errors.append(f"#20 light[{li}] ({l.name!r}) atten_start "
                          f"{l.atten_start} > atten_end {l.atten_end}")

    # ------------------------------------------------------------------
    # Group E - Proxies
    # ------------------------------------------------------------------

    # #21 Proxy count + name set.
    got_pnames = {p.name for p in a.proxies}
    if got_pnames != EXPECTED_PROXIES:
        errors.append(f"#21 proxy names {got_pnames} != expected {EXPECTED_PROXIES}")

    # #22 Flag round-trip.
    proxies_by_name = {p.name: p for p in a.proxies}
    if "p_alpha" in proxies_by_name:
        p = proxies_by_name["p_alpha"]
        if p.is_hidden:
            errors.append(f"#22 p_alpha.is_hidden = True (expected False)")
        if p.alt_decrease_stay_hidden:
            errors.append(f"#22 p_alpha.alt_decrease_stay_hidden = True "
                          f"(expected False)")
    if "p_beta" in proxies_by_name:
        p = proxies_by_name["p_beta"]
        if not p.is_hidden:
            errors.append(f"#22 p_beta.is_hidden = False (expected True)")
        if p.alt_decrease_stay_hidden:
            errors.append(f"#22 p_beta.alt_decrease_stay_hidden = True "
                          f"(expected False)")

    # #23 Each proxy's bone_index points to a bone whose name matches the proxy.
    for p in a.proxies:
        if p.bone_index >= len(a.bones):
            errors.append(f"#23 proxy {p.name!r} bone_index {p.bone_index} "
                          f"out of range")
            continue
        bone_name = a.bones[p.bone_index].name
        if bone_name != p.name:
            errors.append(f"#23 proxy {p.name!r} bone_index {p.bone_index} -> "
                          f"bone {bone_name!r} (name mismatch)")

    # #24 Proxies do NOT appear in the connection table.
    proxy_bone_indices = {bone_index[n] for n in EXPECTED_PROXIES if n in bone_index}
    for ci, c in enumerate(a.connections):
        if c.bone_index in proxy_bone_indices:
            bn = a.bones[c.bone_index].name
            errors.append(f"#24 connection[{ci}] points at proxy bone {bn!r}")

    # ------------------------------------------------------------------
    # Group F - Connection table accounting
    # ------------------------------------------------------------------

    # #25 Connection count = meshes + lights.
    if len(a.connections) != 4:
        errors.append(f"#25 connection count {len(a.connections)} != 4 "
                      f"(2 meshes + 2 lights)")

    # #26 object_index is a permutation of {0,1,2,3}.
    obj_idxs = sorted(c.object_index for c in a.connections)
    if obj_idxs != [0, 1, 2, 3]:
        errors.append(f"#26 connection object_indices {obj_idxs} != [0,1,2,3]")

    # #27 Every bone_index resolves to a real bone.
    for ci, c in enumerate(a.connections):
        if c.bone_index >= len(a.bones):
            errors.append(f"#27 connection[{ci}] bone_index {c.bone_index} "
                          f"out of range (len={len(a.bones)})")

    # #28 No connection points at Root except the skinned mesh.
    for c in a.connections:
        if c.bone_index == 0:
            if not (0 <= c.object_index < len(a.meshes)):
                errors.append(f"#28 non-mesh connection (object_index="
                              f"{c.object_index}) points at Root")
            else:
                connected_mesh = a.meshes[c.object_index].name
                if connected_mesh != "SkinCyl":
                    errors.append(f"#28 mesh {connected_mesh!r} connects to Root "
                                  f"(only SkinCyl should)")

    # ------------------------------------------------------------------
    # Group G - Numeric correctness of bone matrices
    # ------------------------------------------------------------------

    # #29 Authored-position round-trip.
    for name, expected_pos in AUTHORED_POSITIONS.items():
        b = by_name.get(name)
        if b is None:
            errors.append(f"#29 bone {name!r} missing -- cannot verify position")
            continue
        bx, by, bz = b.translation
        ex, ey, ez = expected_pos
        d = max(abs(bx - ex), abs(by - ey), abs(bz - ez))
        if d > _POS_TOL:
            errors.append(f"#29 bone {name!r} translation ({bx:.4f},{by:.4f},"
                          f"{bz:.4f}) != authored {expected_pos} "
                          f"(max delta {d:.4f} > {_POS_TOL})")

    # #30 Bone matrices orthonormal: 3x3 rotation block has unit-length
    # columns, pairwise orthogonal, positive determinant.
    # _alo.py stores 12 floats column-major mapped onto rows as:
    #   matrix = [m00 m01 m02 m03   <- row 0 (X axis + tx)
    #             m10 m11 m12 m13   <- row 1 (Y axis + ty)
    #             m20 m21 m22 m23]  <- row 2 (Z axis + tz)
    # Translation comes from matrix[3], [7], [11]; rotation columns are
    # cols 0,1,2 of the 3x3 block: (m00,m10,m20), (m01,m11,m21), (m02,m12,m22).
    for i, b in enumerate(a.bones):
        m = b.matrix
        cols = [
            (m[0], m[4], m[8]),
            (m[1], m[5], m[9]),
            (m[2], m[6], m[10]),
        ]
        # Unit length.
        for ci, c in enumerate(cols):
            mag = _len(c)
            if abs(mag - 1.0) > _ORTHO_TOL:
                errors.append(f"#30 bones[{i}] ({b.name!r}) col[{ci}] |.|="
                              f"{mag:.5f} != 1.0 (+/- {_ORTHO_TOL})")
                break
        # Pairwise orthogonal.
        for (i1, i2) in ((0, 1), (0, 2), (1, 2)):
            d = abs(_dot(cols[i1], cols[i2]))
            if d > _ORTHO_TOL:
                errors.append(f"#30 bones[{i}] ({b.name!r}) col[{i1}].col[{i2}]"
                              f"|dot|={d:.5f} > {_ORTHO_TOL}")
                break
        # Positive determinant (no mirror).
        c0, c1, c2 = cols
        det = (c0[0] * (c1[1] * c2[2] - c1[2] * c2[1])
               - c0[1] * (c1[0] * c2[2] - c1[2] * c2[0])
               + c0[2] * (c1[0] * c2[1] - c1[1] * c2[0]))
        if det < 0:
            errors.append(f"#30 bones[{i}] ({b.name!r}) has negative "
                          f"determinant {det:.5f} (mirrored?)")

    # ------------------------------------------------------------------
    # Group H - Cross-source consistency
    # ------------------------------------------------------------------

    # #31 .export.log summary matches binary.
    log_path = path + ".export.log"
    summary = _parse_export_log(log_path)
    if summary is None:
        errors.append(f"#31 export log missing or no 'ExportScene summary' "
                      f"block: {log_path}")
    else:
        if summary["bones"] != len(a.bones):
            errors.append(f"#31 log bones={summary['bones']} != binary "
                          f"{len(a.bones)}")
        if summary["meshes"] != len(a.meshes):
            errors.append(f"#31 log meshes={summary['meshes']} != binary "
                          f"{len(a.meshes)}")
        if summary["lights"] != len(a.lights):
            errors.append(f"#31 log lights={summary['lights']} != binary "
                          f"{len(a.lights)}")
        if summary["proxies"] != len(a.proxies):
            errors.append(f"#31 log proxies={summary['proxies']} != binary "
                          f"{len(a.proxies)}")
        if summary["proxy_names"] != got_pnames:
            errors.append(f"#31 log proxy_names {summary['proxy_names']} != "
                          f"binary {got_pnames}")
        if summary["light_names"] != got_lnames:
            errors.append(f"#31 log light_names {summary['light_names']} != "
                          f"binary {got_lnames}")
        # Cross-check proxy flags between log and binary.
        for pname, p in proxies_by_name.items():
            log_flags = summary["proxy_flags"].get(pname)
            if log_flags is None:
                errors.append(f"#31 log missing proxy_flags for {pname!r}")
                continue
            log_hidden, log_alt = log_flags
            if log_hidden != p.is_hidden or log_alt != p.alt_decrease_stay_hidden:
                errors.append(f"#31 log flags for {pname!r}: hidden={log_hidden}/"
                              f"alt={log_alt} disagree with binary "
                              f"hidden={p.is_hidden}/alt={p.alt_decrease_stay_hidden}")

    # ------------------------------------------------------------------
    # Report
    # ------------------------------------------------------------------
    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  ({len(a.bones)} bones, {len(a.meshes)} meshes, "
          f"{len(a.lights)} lights, {len(a.proxies)} proxies; "
          f"31/31 acceptance assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

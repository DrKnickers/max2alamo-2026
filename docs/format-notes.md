# Alamo format notes

Working reference for the `.alo` (model) and `.ala` (animation) chunk formats. This document is the team's source of truth during implementation. Update it when reverse-engineering or testing surfaces new information.

---

## Sources of authority (in descending priority)

1. **Direct observation** of vanilla EaW / FoC files via `alo_dump`, once Phase 1 lands.
2. **Mike Lankamp's MAXScript reader** at `AloImporter-1.2/3dsmax9/alamo2max.ms` — a complete `.alo` and `.ala` parser in readable script. This is the closest thing to a reference implementation we have. Citations below give line numbers in that file.
3. **Strings & layout extracted from `max2alamo.dle`** (EaW 3dsmax8 build) — preserved at `re/output/m2a_eaw_8/strings_raw.txt` (gitignored). Confirms the exporter's vertex format names and `Alamo_*` user-property conventions.
4. **Petrolution chunk spec**: [AloFileFormat](https://modtools.petrolution.net/docs/AloFileFormat) and [AlaFileFormat](https://modtools.petrolution.net/docs/AlaFileFormat).
5. **Gaukler's Blender plugin** ([repo](https://github.com/Gaukler/Blender-ALAMO-Plugin)) — partial executable reference; **does not** cover hardpoints / lights / multi-bone skinning.

When sources disagree, prefer 1 > 2 > 3 > 4 > 5.

---

## Chunk framing

Both `.alo` and `.ala` use a chunked-file framing:

```
struct Chunk {
    uint32_t id;               // little-endian
    uint32_t size_with_flag;   // little-endian
    uint8_t  payload[size_with_flag & 0x7FFFFFFF];
};
```

- `size_with_flag = (payload_size & 0x7FFFFFFF) | (is_container ? 0x80000000 : 0)`. The high bit indicates a **container** chunk (children follow) vs. a **leaf** chunk (raw data). Confirmed by direct observation of vanilla files (e.g. the top-level `0x200` in `W_BUGS.ALO` has size word `0x8000019B`) and matches `alamo2max.ms:250-251`, where `m_size = -1` (sentinel for "in container, no current leaf") is set when the high bit is present.
- Sizes count payload bytes only.
- Container chunks have no inter-chunk padding; children are written back-to-back.

Inside leaf chunks, **mini-chunks** also exist with a smaller header:

```
struct MiniChunk {
    uint8_t id;
    uint8_t size;
    uint8_t payload[size];
};
```

Used for the variable-length parameter blocks in connections (0x601), proxies (0x603), dazzles (0x604), shader params (0x10102–0x10106), and animation bone info (0x1003 / 0x1001). Reference: `alamo2max.ms:224`.

Implemented as `encode_chunk_size` / `decode_payload_size` / `is_leaf_chunk` in `alamo_format/include/alamo_format/chunk_io.h`.

---

## .alo top-level layout

A model file is a stream of chunks at the top level (no enclosing root). Order observed in vanilla files:

1. **`0x200`** Skeleton (always exactly one).
2. Zero or more **`0x400`** (Mesh) and **`0x1300`** (Light), interleaved freely.
3. **`0x600`** Connections (always exactly one, last).

Reference: `alamo2max.ms:647-715`.

---

### Skeleton (`0x200`, container)

| ID | Kind | Payload |
|---|---|---|
| `0x201` | leaf, **always 128 bytes** | `uint32 boneCount` followed by 124 reserved bytes (zero-filled in vanilla content) |
| `0x202` | container, repeated `boneCount` times | per-bone block |

The 124 reserved bytes after `boneCount` in `0x201` were observed to be all-zero across multiple vanilla files (`W_BUGS.ALO`, `ICON_RV_NEBULONB.ALO`, etc.). Likely a fixed-size header for forward compatibility. Our writer emits 4 bytes count + 124 zero bytes.

Per-bone block (`0x202`):

| ID | Kind | Payload |
|---|---|---|
| `0x203` | leaf | null-terminated bone name |
| `0x205` *or* `0x206` | leaf | bone data (see below) |

Bone data payload (`0x205` is base; `0x206` adds billboard mode):

```
uint32  parentIndex     // 0 = root sentinel; importer subtracts 1
uint32  visible         // boolean
uint32  billboardMode   // present only if chunk ID is 0x206
float   matrix[12]      // 4x3 transform, COLUMN-MAJOR (see below)
```

**Bone matrix layout** (the precise bit caught us in Phase 4c — a row-major mistake produced a degenerate transform that collapsed all geometry onto the (1,1,1) diagonal):

The conceptual matrix is 4 rows × 3 columns:
```
| r1x r1y r1z |    row 1 = X axis
| r2x r2y r2z |    row 2 = Y axis
| r3x r3y r3z |    row 3 = Z axis
| tx  ty  tz  |    row 4 = translation
```

On disk: 3 columns of 4 elements, written column-by-column:
```
floats 0..3   = column 0 = (r1x, r2x, r3x, tx)
floats 4..7   = column 1 = (r1y, r2y, r3y, ty)
floats 8..11  = column 2 = (r1z, r2z, r3z, tz)
```

**Identity matrix bytes**: `1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0`.

Mike's reader (`alamo2max.ms:374-378`) does the inverse: reads `c[1..12]` sequentially and builds the Max `Matrix3` via `[c[1],c[5],c[9]] [c[2],c[6],c[10]] [c[3],c[7],c[11]] [c[4],c[8],c[12]]` — i.e. row 1 takes elements 1, 5, 9 (one from each column), which only makes sense if the file stores columns first.

**Vanilla static-prop convention** (Phase 4c): every mesh-bearing model has at least **two bones in the skeleton** — the synthetic `Root` (parent = 0xFFFFFFFF) plus a named non-Root bone per mesh. Mike's importer (`alamo2max.ms:653`) deletes Root after reading; vertex / connection bone references must land on real bones or his skin setup crashes Max. EaW's loader appears to share this expectation. Our writer always emits at least Root + one named bone per `ExportMesh`.

Reference: `alamo2max.ms:341-385`.

---

### Mesh (`0x400`, container)

| ID | Kind | Payload |
|---|---|---|
| `0x401` | leaf | null-terminated mesh name |
| `0x402` | leaf, **always 128 bytes** | mesh metadata (see below); 88 reserved bytes after the documented fields |
| `0x10100` | container, repeated per material | submesh material + geometry |

Mesh metadata (`0x402`, always 128 bytes total):

```
uint32  materialCount         // offset 0
float   bbox_or_other[6]      // offset 4..28  (bounding box; layout TBD - Mike's reader skips them)
uint32  unused                // offset 28..32 (skipped by importer)
uint32  isHidden              // offset 32..36
uint32  isCollisionMesh       // offset 36..40
uint8   reserved[88]          // offset 40..128 (zero-padded in vanilla content)
```

The 128-byte fixed size was confirmed across 10,678 `0x402` chunks in the vanilla corpus - same fixed-size-header pattern as `0x201`. Reference: `alamo2max.ms:546-550`.

---

### Submesh material (`0x10100`, container) — Material chunk ONLY

| ID | Kind | Payload |
|---|---|---|
| `0x10101` | leaf | shader name (e.g. `MeshBumpColorize.fx`) |
| `0x10102` … `0x10106` | leaf, repeated | shader parameters (mini-chunks: `1`=name, `2`=value) |

**Important (Phase 4c correction):** `0x10000` (geometry) is **a sibling of `0x10100` inside the parent `0x400`, not a child.** Vanilla layout per submesh:

```
0x400 Mesh
  0x401 mesh name
  0x402 mesh info
  0x10100 material[0]      <-- shader + params only
  0x10000 geometry[0]      <-- SIBLING of 0x10100 at this level
  0x10100 material[1]      <-- multi-material alternates
  0x10000 geometry[1]
  ...
```

Mike's `ReadMaterial` (`alamo2max.ms:455-535`) walks `0x10100`'s children with `Next()` until it hits the end-of-container sentinel, then calls `Next()` again — which reads the *next sibling* `0x10000` inside the parent `0x400`. We had this nested initially and AloViewer / EaW rejected the file.

Shader parameter chunk → type mapping:

| Chunk | Param type | Value encoding |
|---|---|---|
| `0x10102` | INT | `int32` |
| `0x10103` | FLOAT | `float` |
| `0x10104` | FLOAT3 | 3 × `float` |
| `0x10105` | TEXTURE | null-terminated string |
| `0x10106` | FLOAT4 | 4 × `float` |

Reference: `alamo2max.ms:387-424`.

Geometry block (`0x10000`, container — sibling of `0x10100` inside `0x400`):

| ID | Kind | Payload |
|---|---|---|
| `0x10001` | leaf, **always 128 bytes** | `uint32 vertexCount` + `uint32 faceCount` + 120 reserved zero bytes |
| `0x10002` | leaf | vertex format name as null-terminated string (e.g. `alD3dVertNU2\0`) |
| `0x10005` *or* `0x10007` | leaf | vertex data (see vertex layouts below) |
| `0x10004` | leaf | face data: `faceCount` × `uint16[3]` indices |
| `0x10006` | leaf, optional | bone mapping table: `uint32[]` of length `chunk_size / 4` |
| `0x1200` | container, optional, only on collision meshes | collision tree |

**`0x10001` is fixed-size 128 bytes** (Phase 4c correction): Mike's reader only consumes the first 8 bytes (counts) and uses `Next()` to skip the rest, but AloViewer rejects shorter chunks. Same fixed-size-with-reserved-tail pattern as `0x201` and `0x402`. Confirmed across the vanilla corpus.

Reference: `alamo2max.ms:455-535`.

---

## Vertex layouts — RESOLVED

**Critical finding from corpus survey (Phase 4b):** every single vertex chunk in the vanilla EaW + FoC corpus uses one of **two physical sizes — 128 bytes per vertex (chunk `0x10005`, "rev 1") or 144 bytes per vertex (chunk `0x10007`, "rev 2") — regardless of the format name in `0x10002`.** The format name selects which fields the engine *uses*; the on-disk size is always the full B4I4 layout. Confirmed across 9000+ submeshes (`re/scripts/survey-vertex-formats.ps1`).

What this means for our writer:
- Always emit `0x10007` (rev 2 dominates: ~99% of vanilla submeshes).
- Always write 144 bytes per vertex in B4I4 layout.
- Fill the fields named by the chosen format name with real data; fill the rest with neutral defaults (zero UVs / tangent / binormal, white color, alpha=1, bone index 0, weight 1).
- The format-name string in `0x10002` is what tells EaW which slots to interpret — *not* the vertex chunk size.

Shader / format pairings observed in vanilla content (top hits):

| Shader | Vertex format | Occurrences |
|---|---|---|
| `alDefault.fx` | `alD3dVertNU2` | 1980 |
| `MeshBumpColorize.fx` | `alD3dVertNU2U3U3` | 1855 |
| `MeshAlpha.fx` | `alD3dVertNU2` | 1356 |
| `MeshShadowVolume.fx` | `alD3dVertN` | 1402 |
| `MeshAdditive.fx` | `alD3dVertNU2` | 1079 |
| `MeshCollision.fx` | `alD3dVertN` | 852 |

Our Phase 4 default (`MeshAlpha.fx` + `alD3dVertNU2`) is well-attested.

The exporter binary contains **15 named vertex format strings** (extracted from `max2alamo.dle` strings table):

| Format | Skin | Layout summary |
|---|---|---|
| `alD3dVertN` | none | pos + normal |
| `alD3dVertNC` | none | pos + normal + color |
| `alD3dVertNU2` | none | pos + normal + UV |
| `alD3dVertNU2C` | none | pos + normal + UV + color |
| `alD3dVertNU2U3` | none | + tangent |
| `alD3dVertNU2U3U3` | none | + tangent + binormal |
| `alD3dVertNU2U3U3C` | none | full + color |
| `alD3dVertRSkinNU2` | rigid (1 bone) | RSkin variant |
| `alD3dVertRSkinNU2C` | rigid | RSkin + color |
| `alD3dVertRSkinNU2U3U3` | rigid | RSkin + bumpy |
| `alD3dVertB4I4NU2` | up to 4 bones | B4I4 base |
| `alD3dVertB4I4NU2C` | 4 | B4I4 + color |
| `alD3dVertB4I4NU2U3U3` | 4 | B4I4 + bumpy |
| `alD3dVertB4I4NU2U3U3C` | 4 | full B4I4 |
| `alD3dVertGrass` | special | grass billboards |

**Two skinning modes exist:**

- **`RSkin*`** (rigid skin) — one bone per vertex via per-mesh `0x10006` bone-mapping table. The Blender plugin's "1 bone, weight 1.0" simplification is actually emitting this mode.
- **`B4I4*`** (4 bones, 4 indices) — full multi-bone weighted skinning. **This is what the MAXScript reader's `ReadVertex_1` / `ReadVertex_2` parses** (`alamo2max.ms:426-453`).

User control: the `_ALAMO_BONES_PER_VERTEX` MAXScript hook on a mesh selects between these (and the shader choice forces it — `RSkin*.fx` shaders demand RSkin layout, `*Bump*Skin*.fx` etc. demand B4I4).

### B4I4 vertex layout (chunk `0x10005` "revision 1")

```
float    pos[3]            // 12 bytes
float    normal[3]         // 12 bytes
float    uv0[2]            // 8 bytes
float    uv1[2]            // 8 bytes
float    uv2[2]            // 8 bytes
float    uv3[2]            // 8 bytes  (uv1..uv3 may be unused by shader)
float    tangent[3]        // 12 bytes
float    binormal[3]       // 12 bytes
float    color[3]          // 12 bytes
float    alpha             // 4 bytes
uint32   boneIndex[4]      // 16 bytes
float    boneWeight[4]     // 16 bytes
                           // = 128 bytes per vertex
```

### B4I4 vertex layout (chunk `0x10007` "revision 2")

Same as revision 1, but with **16 extra bytes of unused data** between `alpha` and `boneIndex[4]` (the importer skips them). 144 bytes per vertex total. Reference: `alamo2max.ms:440-453`.

V is flipped on read (`uv.v = 1 - reader.GetFloat()` at `alamo2max.ms:431`), so we **write** `v -> 1 - v` likewise.

### RSkin and non-skinned vertex layouts -- RESOLVED

**They don't have separate physical layouts.** Per the corpus survey above, every vertex chunk uses the 128 / 144 byte B4I4 shape regardless of format name. The previously inferred sizes per name suffix were a red herring. Disregard the table below -- kept for historical reference of what we initially assumed.

~~Inferred layouts (now disproven):~~

```
alD3dVertN              : pos + normal                                                           = 24 bytes
alD3dVertNC             : pos + normal + color                                                   = 36 bytes
alD3dVertNU2            : pos + normal + uv                                                      = 32 bytes
alD3dVertNU2C           : pos + normal + uv + color                                              = 44 bytes
alD3dVertNU2U3          : pos + normal + uv + tangent                                            = 44 bytes
alD3dVertNU2U3U3        : pos + normal + uv + tangent + binormal                                 = 56 bytes
alD3dVertNU2U3U3C       : pos + normal + uv + tangent + binormal + color                         = 68 bytes
alD3dVertRSkinNU2       : pos + normal + uv + boneIdx(uint32)                                    = 36 bytes
alD3dVertRSkinNU2C      : pos + normal + uv + color + boneIdx                                    = 48 bytes
alD3dVertRSkinNU2U3U3   : pos + normal + uv + tangent + binormal + boneIdx                       = 60 bytes
alD3dVertB4I4NU2        : pos + normal + uv + boneIdx[4] + boneWeight[4]                         = 64 bytes
alD3dVertB4I4NU2C       : + color                                                                = 76 bytes
alD3dVertB4I4NU2U3U3    : + tangent + binormal                                                   = 88 bytes
alD3dVertB4I4NU2U3U3C   : full                                                                   = 100 bytes
```

**These inferred sizes need verification against `alo_dump` output on vanilla files in Phase 1.** Mark as TBD until then.

---

### Light (`0x1300`, container)

| ID | Kind | Payload |
|---|---|---|
| `0x1301` | leaf | null-terminated light name |
| `0x1302` | leaf | light data |

Light data payload:

```
uint32  type                  // 0=Omni, 1=Directional, 2=Spot
float   color[3]              // RGB in [0,1]
float   intensity
float   farAttenuationEnd
float   farAttenuationStart
float   hotspotSize           // spot lights only (still written for omni/dir; ignored)
float   falloffSize
```

Reference: `alamo2max.ms:562-582`.

---

### Connections (`0x600`, container)

| ID | Kind | Payload |
|---|---|---|
| `0x601` | leaf | counts (mini-chunks) |
| `0x602` | leaf, repeated | object → bone connection (mini-chunks) |
| `0x603` | leaf, repeated | proxy (mini-chunks) |
| `0x604` | leaf, repeated, **FoC/UaW only** | dazzle (mini-chunks) |

`0x601` mini-chunk fields:

| Mini ID | Field |
|---|---|
| `1` | `uint32 nConnections` |
| `4` | `uint32 nProxies` |
| `9` | `uint32 nDazzles` (optional, only when present) |

`0x602` mini-chunk fields:

| Mini ID | Field |
|---|---|
| `2` | `uint32 objectIndex` (index into combined `meshes + lights` array) |
| `3` | `uint32 boneIndex` (0 = root sentinel; subtract 1) |

`0x603` (proxy) mini-chunk fields:

| Mini ID | Field |
|---|---|
| `5` | name (string) |
| `6` | `uint32 boneIndex` |
| `7` | `uint32 isHidden` (optional) |
| `8` | `uint32 altDecreaseStayHidden` (optional) |

Reference: `alamo2max.ms:584-715`.

---

## Hardpoints — clarification

The Alamo format has **no separate hardpoint chunk type.** Hardpoints are just **proxies** (`0x603` chunks); the "hardpoint" concept lives in the game's XML-side configuration that references proxy names. Petroglyph's exporter writes a count of "Proxy-objects" in its log output (`max2alamo.dle` strings table), and Mike's importer creates Max dummies named after the proxy name.

**Naming convention:** community practice is `HP_<weapon_name>` for hardpoints (e.g. `HP_TurboLaser_01`), but the format itself doesn't enforce this. The Petroglyph exporter writes whatever the helper's name is.

---

## Helper-object conventions — RESOLVED

The legacy Petroglyph exporter reads these **user properties** from Max scene nodes (extracted from `max2alamo.dle` strings):

| User property name | Applies to | Effect |
|---|---|---|
| `Alamo_Export_Geometry` | mesh | mark as exportable mesh |
| `Alamo_Export_Transform` | bone / dummy | mark as exportable bone |
| `Alamo_Geometry_Hidden` | mesh | sets `isHidden` flag in `0x402` |
| `Alamo_Collision_Enabled` | mesh | sets `isCollisionMesh` flag in `0x402` |
| `Alamo_Billboard_Mode` | bone | sets billboard mode → emit `0x206` instead of `0x205` |
| `Alamo_Alt_Decrease_Stay_Hidden` | proxy | sets the optional `8` mini-chunk in `0x603` |

MAXScript hooks (mesh-level, scene-level):

| Hook | Effect |
|---|---|
| `_ALAMO_BILLBOARDS` | enable billboard processing on a mesh |
| `_ALAMO_BONES_PER_VERTEX` | choose RSkin (1) vs B4I4 (4) vertex layout |
| `_ALAMO_SHADOW_VOLUME` | mark as shadow volume mesh |
| `_ALAMO_TANGENT_SPACE` | force tangent/binormal generation |
| `_ALAMO_VERTEX_TYPE` | override vertex format selection |
| `_ALAMO_Z_SORT` | enable Z-sorting (likely transparent meshes) |

Our Max 2026 plugin should accept the same property names so existing Max scenes ported from EaW Mod Tools work without re-tagging.

---

## .ala animation format

Top-level: a single container `0x1000`.

```
0x1000 (container) "Animation root"
  0x1001 (leaf) "Animation info"
    mini 1  uint32 nFrames
    mini 2  float  fps
    mini 3  uint32 nBones
    [FoC only:]
    mini 11 uint32 nRotationWords
    mini 12 uint32 nTranslationWords
    mini 13 uint32 nScaleWords
  0x1002 (container, repeated nBones times) "Animation bone"
    0x1003 (leaf) "Animation bone information"
      mini 4  string  bone name
      mini 5  uint32  bone index in skeleton
      mini 10 (optional, ignored)
      mini 6  float[3] translation offset
      mini 7  float[3] translation scale
      mini 8  float[3] scale offset
      mini 9  float[3] scale scale
      [FoC only:]
      mini 14 int16   idxTrans       (-1 = no translation track)
      mini 15 int16   idxScale       (-1 = no scale track)
      mini 16 int16   idxRotation    (-1 = no rotation track)
      mini 17 int16[4] defaultRotation (packed quaternion)
    [EaW only — per-bone tracks live inside the 0x1002 block:]
    0x1004 (leaf, optional) translation data — nFrames × int16[3], unpacked via offset+scale
    0x1005 (leaf, optional) scale data        — nFrames × int16[3]
    0x1006 (leaf)           rotation data — either int16[4] (single default quat) or nFrames × int16[4]
    0x1007 (leaf, optional) visibility track  — bit-packed, 1 bit per frame (see below)
    0x1008 (leaf, optional) unknown / ignored
  [FoC only — track data lives at file scope after all 0x1002 bones:]
  0x100a (leaf, present iff nTranslationWords > 0) translation words: nFrames × nTranslationWords × int16
  0x1009 (leaf, present iff nRotationWords > 0)    rotation words:    nFrames × nRotationWords × int16
```

Reference: `alamo2max.ms:749-931`.

### Quaternion compression — RESOLVED

Stored as 4 × `int16`, scale factor `1/32767.0`:

```
on read:   q.x = (int16)bytes[0..1] / 32767.0  (and y, z, w)
on write:  bytes = round(q * 32767)            (and y, z, w)
```

Order: **XYZW**. Reference: `alamo2max.ms:727-736`.

### Position / scale compression

Stored as 3 × `uint16`, unpacked with per-bone offset + scale:

```
on read:   pos = uint16[3] * transScale + transOffset  (per-bone unpack)
on write:  for each bone:
             find min/max position over all frames
             transScale  = (max - min) / 65535
             transOffset = min
             for each frame:
                pos_packed = round((pos - transOffset) / transScale)  -- clamped to [0, 65535]
```

(Same scheme for scale, with `scaleOffset` / `scaleScale`.) Reference: `alamo2max.ms:738-747` + `unpacks` usage at lines 763-799.

### Visibility track encoding — RESOLVED

Per-frame, 1 bit per frame, **LSB first within each byte**:

```
write:
  bytes = []
  for each byte_idx in 0 .. ceil(nFrames/8) - 1:
      val = 0
      for bit_idx in 0 .. 7:
          frame = byte_idx * 8 + bit_idx
          if frame < nFrames and visibility[frame]:
              val |= (1 << bit_idx)
      bytes.append(val)
read:
  for f in 0 .. nFrames-1:
      if f % 8 == 0: val = bytes[f / 8]
      visible = (val & 1) != 0
      val >>= 1
```

Reference: `alamo2max.ms:818-833`.

`hasVisibilityTrack` is set whenever any frame is hidden — i.e. the visibility chunk is **only emitted if at least one frame differs from "visible"**. For our exporter: scan visibility for the bone; emit `0x1007` only if any frame is hidden.

### EaW vs FoC track storage

Two different encodings, both common in vanilla content:

- **EaW** (older): per-bone tracks stored inside each `0x1002` block as separate `0x1004` / `0x1005` / `0x1006` leaves. Each value is a fully-resolved `int16[3]` or `int16[4]`. No deduplication.
- **FoC** (newer): tracks share a global word pool at file scope (`0x100a` translations, `0x1009` rotations). Each bone references its slot in the pool via `idxTrans` / `idxScale` / `idxRotation` indices. Deduplication is possible (multiple bones can share an idx).

For v1, **emit FoC format** by default — it's smaller, both engines support it, and it matches what vanilla FoC ships. Add EaW format as an option if needed.

### Multi-clip filename convention — RESOLVED (Phase 11b.1)

Per-asset animations ship as **one `.ala` file per clip**, named `<basename>_<CLIPNAME>.ala` as a sibling of `<basename>.alo`. The clip name lives only in the filename — the `.ala` chunk format has no on-disk clip-name field. Validated against three sources, all converging:

- **Vanilla EaW + FoC corpus** (2863 files surveyed in Phase 11a): every `.ala` follows `<UNIT>_<CLIP>.ALA` or `<UNIT>_<CLIP>_<INDEX>.ALA` (variant takes). Infantry units catalogue 20–56 clips each; `EI_TROOPER` peaks at 56.
- **Mike Lankamp's `alamo2max.ms`** importer auto-discovers via the glob `GetFiles((path) + basename + "_*.ALA")` (line 1535) and extracts the clip name by stripping the `<basename>_` prefix and `.ala` suffix (line 1557).
- **Gaukler's Blender plugin** treats each `.ala` as a single Blender action named after the filename; users batch-export by re-running the operator per clip.

Our walker authors the clip list on the scene root via two user-prop conventions:

- **Multi-clip path:** `Alamo_Anim_Clips = "WALK|ATTACK|IDLE"` (pipe-delimited list) plus per-clip `Alamo_Anim_<NAME>_Start` / `_End` ranges. Each declared clip emits a `<basename>_<NAME>.ala` sibling. Malformed clips (missing `_Start`/`_End`, inverted range) are skipped — the other clips still emit (partial-export policy).
- **Single-clip back-compat:** un-suffixed `Alamo_Anim_Start` / `_End` / `_Name` emits a bare `<basename>.ala` (Phase 8b/c/d shape).

When both conventions are authored on the same scene, **the multi-clip path wins deterministically** — the un-suffixed un-prefixed `Alamo_Anim_Start/_End/_Name` props are ignored, no bare `<basename>.ala` emits.

---

## Open questions

| # | Question | Resolution path |
|---|---|---|
| 1 | ~~RSkin and non-skinned vertex layouts: confirm byte sizes inferred from name suffixes.~~ **Resolved Phase 4b:** all formats use the same 128 / 144-byte B4I4 layout; format name selects field interpretation, not size. |
| 2 | `0x402` mesh metadata: bbox layout (6 floats min/max vs other?). | Internal layout still TBD; container size is **resolved** (always 128 bytes; documented fields take 40 bytes, remaining 88 are reserved). Decoder confirmed via cross-check on collision meshes (`hidden=1 collision=1` only on meshes with `MeshCollision.fx`). |
| 2b | `0x201` reserved 124 bytes: confirmed all-zero across sampled files. **Resolved.** | --- |
| 3 | ~~`0x10002` vertex format chunk payload: just the format string? Format flags?~~ **Resolved Phase 4b:** just the null-terminated format-name string (e.g. `alD3dVertNU2\0`). Confirmed in `RV_XWING.ALO` (multiple instances dumped) and across the corpus. |
| 4 | Format-version indicator: how does the engine distinguish EaW-style vs FoC-style `.ala` for a given file? | Phase 8: presence/absence of mini-chunks 11/12/13 in `0x1001` is the signal (per `alamo2max.ms:855-861`). |
| 5 | Collision tree (`0x1200`-`0x1203`) internal structure. | Phase 4 / 5 if collision export is needed; not required for static rendering. |
| 6 | What does `_ALAMO_VERTEX_TYPE` accept as values? | Confirm via Phase 4 RE of the exporter binary if Standard-material → vertex-format inference proves insufficient. |
| 7 | ~~Bone matrix on-disk byte order.~~ **Resolved Phase 4c:** 3 columns of 4 elements stored column-major (NOT row-major). Identity = `1,0,0,0, 0,1,0,0, 0,0,1,0`. Row-major mistake produces a degenerate transform that collapses all geometry. |
| 8 | ~~`0x10100` and `0x10000` nested?~~ **Resolved Phase 4c:** they are SIBLINGS inside `0x400`, alternating per submesh (`0x10100` material then `0x10000` geometry). NOT nested. |
| 9 | ~~`0x10001` size~~ **Resolved Phase 4c:** fixed-size 128 bytes (8 used + 120 reserved zeros). Same pattern as `0x201` / `0x402`. |
| 10 | ~~Minimum skeleton size for static props.~~ **Resolved Phase 4c:** vanilla static-prop models always have **Root + at least one named non-Root bone per mesh**. Mike's importer crashes on Root-only skeletons (deletes Root, then dangles on vertex bone references); EaW appears to share this expectation. |

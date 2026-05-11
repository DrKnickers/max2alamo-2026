# Alamo format notes

Working reference for the `.alo` (model) and `.ala` (animation) chunk formats. This document is the team's source of truth during implementation. Update it when reverse-engineering or testing surfaces new information.

---

## Sources of authority (in descending priority)

1. **Direct observation** of vanilla EaW / FoC files via `alo_dump`, once Phase 1 lands.
2. **Reverse-engineering** of `max2alamo.dle` and `alamo2max.dlu` (Phase 0.5; results paraphrased here).
3. **Petrolution chunk spec**: [AloFileFormat](https://modtools.petrolution.net/docs/AloFileFormat) and [AlaFileFormat](https://modtools.petrolution.net/docs/AlaFileFormat).
4. **Gaukler's Blender plugin** ([repo](https://github.com/Gaukler/Blender-ALAMO-Plugin), file paths under `io_alamo_tools/`) — executable reference, but with known gaps documented below.

When sources disagree, prefer 1 > 2 > 3 > 4 and note the discrepancy here.

---

## Chunk framing

Both `.alo` and `.ala` use Petroglyph's chunked-file framing:

```
struct Chunk {
    uint32_t id;
    uint32_t size_with_flag;   // little-endian
    uint8_t  payload[size_with_flag & 0x7FFFFFFF];
};
```

- `size_with_flag` = `(payload_size & 0x7FFFFFFF) | (is_leaf_chunk ? 0x80000000 : 0)`.
- The high bit indicates **leaf** (data) vs **container** (holds further chunks). Source: `chunk_size()` in `Blender-ALAMO-Plugin/io_alamo_tools/export_alo.py`.
- Sizes count payload bytes only; the 8-byte header is not included.
- Container chunks have no inter-chunk padding; children are written back-to-back.

Implemented as `encode_chunk_size` / `decode_payload_size` / `is_leaf_chunk` in `alamo_format/include/alamo_format/chunk_io.h`.

---

## .alo top-level chunks

| ID | Type | Purpose |
|---|---|---|
| `0x200` | container | Skeleton |
| `0x400` | container | Mesh |
| `0x1300` | container | Light |
| `0x600` | container | Connections |

Multiple `0x400` and `0x1300` chunks may appear; one `0x200` and one `0x600`. Order observed in vanilla files: skeleton, meshes, lights, connections.

### Skeleton (`0x200`)

| ID | Notes |
|---|---|
| `0x201` | bone count (leaf) |
| `0x202` | per-bone container |
| `0x203`–`0x206` | per-bone leaves: name, parent index, billboard / visibility flags, transform matrix |

Exact field layout to be confirmed by Phase 0.5 RE.

### Mesh (`0x400`)

| ID | Notes |
|---|---|
| `0x401` | mesh name (leaf) |
| `0x402` | mesh info: material count, bounding box, visibility, collision flags |
| `0x10000` | submesh container (one per material) |
| `0x10001`–`0x10007` | submesh leaves: vertex buffer, index buffer, animation mappings |
| `0x10100` | submesh material container |
| `0x10101`–`0x10106` | shader-parameter leaves: INT, FLOAT, FLOAT3, FLOAT4, TEXTURE |

### Lights (`0x1300`)

| ID | Notes |
|---|---|
| `0x1301` | light type / color / intensity |
| `0x1302` | attenuation parameters |

Not implemented in the Blender plugin → primary RE target in Phase 0.5.

### Connections (`0x600`)

| ID | Notes |
|---|---|
| `0x601` | connection counts |
| `0x602` | object-to-bone mapping (which mesh attaches to which bone) |
| `0x603` | proxy connections |

### Collision (sub-chunks under `0x402`?)

| ID | Notes |
|---|---|
| `0x1200`–`0x1203` | spatial-tree structures |

Blender plugin builds a median-cut bounding-box tree with triangle leaf nodes. Position in the chunk hierarchy to confirm by RE / `alo_dump`.

---

## Coordinate convention

**Identity for vertex and bone data.** The Blender plugin (`io_alamo_tools/export_alo.py`) writes Blender Z-up RH coordinates directly to the file with no axis swap. Max is also Z-up RH. The only documented transform is **UV V-coordinate inversion** (`v -> -v`) at write time.

Hold this assumption until we observe a counter-example in vanilla files.

---

## Vertex packing

Petroglyph uses named vertex format types selected per shader, per `vertex_format_dict` in `Blender-ALAMO-Plugin/io_alamo_tools/settings.py`:

| Format | Layout |
|---|---|
| `alD3dVertN` | position, normal |
| `alD3dVertNU2` | position, normal, UV |
| `alD3dVertNU2U3U3` | position, normal, UV, tangent, bitangent |
| `alD3dVertNU2C` | position, normal, UV, color |
| `alD3dVertRSkinNU2` | rigged: position, normal, UV, bone indices, weights |
| `alD3dVertRSkinNU2U3U3` | rigged + tangent/bitangent |

Exact byte layouts (alignment, component types) to be pinned down by RE in Phase 0.5. Tangent/bitangent are only generated for shaders in `bumpMappingList`.

### Skin weights

The Blender plugin ships **a single bone with weight 1.0 per vertex** (see `getMaxWeightGroupIndex()` in `export_alo.py`). This is a Blender-side simplification; vanilla EaW models use up to 4 bone influences per vertex. The real packing is one of the **primary RE targets** in Phase 0.5.

Likely layout (to verify): `int8 boneIdx[4]` followed by `float weight[3]` (with the 4th weight implied as `1 - sum`), or `int8 boneIdx[4]` + `int8 normalizedWeight[4]`. Will be confirmed by examining the original exporter's vertex-buffer-write function in Ghidra.

---

## Material parameters

Per `material_parameter_dict` in `Blender-ALAMO-Plugin/io_alamo_tools/settings.py`, ~30 stock Petroglyph shaders exist. The most common in vanilla EaW models include:

- `MeshBumpColorize.fx` — Emissive, Diffuse, Specular, Shininess, Colorization, BaseTexture, NormalTexture
- `MeshBumpColorizeDetail.fx` — adds DetailTexture, NormalDetailTexture, MappingScale, BlendSharpness
- `MeshShadowVolume.fx` — for shadow volumes (manifold geometry required)
- `RSkinBumpColorize.fx` — rigged variant
- `BatchMeshAlpha.fx`, `BatchMeshGloss.fx` — alpha-blended / glossy
- `Planet.fx` — planets (atmosphere, clouds)
- `MeshShield.fx` — shield effects
- `alDefault.fx` — fallback (no parameters)

These dictate vertex format selection and which `0x10101`–`0x10106` parameter chunks must be emitted.

---

## .ala animation format

Each `.ala` is a separate file paired by base name with its `.alo`.

### Quaternion compression

Per `Blender-ALAMO-Plugin/io_alamo_tools/export_ala.py`:

```
int16 = round(component * 32767)
```

Component order is **XYZW** (writer emits Y, Z, X, W in that loop order — verify against RE; the spec calls XYZW but the writer iterates differently).

### Frame sampling

The Blender plugin samples every frame between 0 and `animLength` via `scene.frame_set()`. No keyframe reduction or curve fitting. Position and scale are stored as float; rotation as int16-quantized quaternions.

### Visibility tracks

Per-bone, per-frame visibility encoded as a packed bit string:

- '1' = visible, '0' = hidden
- Packed into bytes, each byte's bit order reversed for little-endian: `byte = bits[0:8][::-1]`

### Bone tracks

Each bone has a name mini-chunk (ID `\x04`) and an index mini-chunk (ID `\x05\x04`), followed by per-frame transformation offsets and scale factors. Exact chunk IDs and layout to be confirmed via RE + `alo_dump` once `.ala` parsing is added.

---

## Helper objects (hardpoints, proxies, dazzles)

The Blender plugin handles **proxies only** (via custom EditBone properties: `EnableProxy`, `ProxyName`, `proxyIsHidden`, `altDecreaseStayHidden`).

**Hardpoints, dazzles, and lights** are not implemented in the Blender plugin. We need RE of the original `max2alamo.dle` to recover:

- Naming conventions (likely `HP_*` for hardpoints, `Dazzle_*` for dazzles).
- User-property keys read by Petroglyph's exporter (e.g. `HardpointType`, `FireBoneName`).
- The chunk(s) emitted to represent each kind.

Mike Lankamp's `alamo2max.dlu` (the importer) reads these chunks and is a useful secondary RE oracle for the read side.

---

## Open questions

- [ ] Multi-bone skin vertex layout: byte order, weight quantization. **Phase 0.5 RE.**
- [ ] Hardpoint chunk ID and field layout. **Phase 0.5 RE.**
- [ ] Light chunk full field layout (color encoding: float32 RGB? RGBA? gamma-encoded?). **Phase 0.5 RE.**
- [ ] Collision tree construction algorithm (Blender plugin uses median-cut; confirm Petroglyph does the same). **Phase 0.5 RE / `alo_dump` comparison.**
- [ ] Differences (if any) between EaW and FoC `.alo` files. **Phase 1 corpus survey.**
- [ ] `.ala` chunk IDs and structure beyond what the Blender plugin shows. **Phase 8 RE.**

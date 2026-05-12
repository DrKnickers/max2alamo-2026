# `shaders/max-preview/` — Effects11 stubs for 3ds Max 2026 authoring

These `.fx` files exist **only so 3ds Max 2026 can load a DirectX Shader material whose filename matches a Petroglyph stock shader, and surface the same parameter UI Petroglyph's shader did.** They are *not* shipped into EaW mods, and they have no effect on in-game rendering.

## Why this folder exists

Max 2026's DirectX Shader material runtime rejects Petroglyph's stock `.fx` files. The PG shaders are written for the DX9 effect framework (`fx_2_0` profile, inline render states, `VertexShader = (compiled_var)` assignment style). Max 2026's runtime expects Effects11 (`technique11`, state objects, `SetVertexShader(CompileShader(vs_5_0, ...))`). The two frameworks are not bridgeable by patching; the entire technique/pass structure must be rewritten.

Without these stubs, the canonical Petroglyph authoring workflow —

> Material Editor → DirectX Shader → load `MeshBumpColorize.fx` → parameter rollout appears → wire textures → preview → export

— breaks at "load." This folder fixes that by providing same-named Effects11 stubs that:

- have the same parameter names, types, and `UIName` annotations as the PG shaders
- render a simplified Blinn/Lambert approximation in the Max viewport (visual fidelity is *not* the goal — UI fidelity is)
- exist purely Max-side; the exported `.alo` references only the shader filename, and EaW resolves it against its own `Data/Art/Shaders/` at runtime

## How `max2alamo` uses the stub

When you export, `max2alamo` reads the shader filename from the DirectX Shader material via `IDxMaterial::GetEffectFile()` and writes that filename into the `.alo`. The filename is what matters — `MeshBumpColorize.fx` in the `.alo` means "use the engine's real MeshBumpColorize shader." Our stub never travels with the model.

Per-parameter writeback is driven by `material_parameter_dict` (ported from Gaukler's Blender plugin); parameters not in that dict are ignored regardless of whether the stub exposes them.

## Workflow

1. In Max 2026, apply a DirectX Shader material to your mesh.
2. Set its effect file to `shaders/max-preview/<ShaderName>.fx` from your clone of this repo.
3. The parameter rollout appears with the same names PG uses.
4. Wire textures and tweak values; the viewport renders the stub's approximation.
5. Export via `max2alamo` — the `.alo` references `<ShaderName>.fx`. AloViewer and the EaW runtime use their own real shaders.

For shaders without a stub yet, use the `Alamo_Shader_Name` node user-property override as a fallback.

## How these stubs are generated

The whole folder is auto-generated from a manifest in [scripts/generate-max-preview-stubs.py](../scripts/generate-max-preview-stubs.py). Each manifest entry pairs a shader filename with its param list (sourced from Gaukler's `material_parameter_dict` and the PG `.fxh` headers in `corruption/Mods/Empire-at-War-Source-Files/src/FOC/Data/Art/Shaders/`) and a render mode (`lit-opaque` / `lit-alpha` / `lit-additive` / `flat-color` / `flat-alpha`).

To add a stub for a new PG shader: edit the `SHADERS` list in that script and re-run `python scripts/generate-max-preview-stubs.py`. Do not hand-edit the generated `.fx` files — the next regeneration will clobber edits.

## DO NOT

- **Do not** copy these `.fx` files into an EaW mod's `Data/Art/Shaders/` folder. The engine has its own real shaders by these names; replacing them with these stubs will visibly downgrade in-game rendering.
- **Do not** treat the stub's preview as authoritative. It's "close enough for authoring," not pixel-perfect parity with the engine.
- **Do not** hand-edit the generated `.fx` files — see "How these stubs are generated" above.

## Coverage

All Petroglyph shaders from `corruption/Mods/Empire-at-War-Source-Files/src/FOC/Data/Art/Shaders/` are covered. The `Loads in Max 2026` column marks shaders whose end-to-end workflow (load → preview → export → AloViewer round-trip) has been validated.

| Shader                          | Stub | Loads in Max 2026 |
|---------------------------------|:----:|:-----------------:|
| `alDefault.fx`                  |  ✓   |        ✓          |
| `BatchMeshAlpha.fx`             |  ✓   |        —          |
| `BatchMeshGloss.fx`             |  ✓   |        —          |
| `BlobStencilMasked.fx`          |  ✓   |        —          |
| `Grass.fx`                      |  ✓   |        —          |
| `MeshAdditive.fx`               |  ✓   |        —          |
| `MeshAdditiveOffset.fx`         |  ✓   |        —          |
| `MeshAdditiveVColor.fx`         |  ✓   |        —          |
| `MeshAlpha.fx`                  |  ✓   |        —          |
| `MeshAlphaGloss.fx`             |  ✓   |        —          |
| `MeshAlphaScroll.fx`            |  ✓   |        —          |
| `MeshBumpColorize.fx`           |  ✓   |        —          |
| `MeshBumpReflectColorize.fx`    |  ✓   |        —          |
| `MeshCollision.fx`              |  ✓   |        —          |
| `MeshGloss.fx`                  |  ✓   |        —          |
| `MeshGlossColorize.fx`          |  ✓   |        —          |
| `MeshHeat.fx`                   |  ✓   |        —          |
| `MeshLightVisualize.fx`         |  ✓   |        —          |
| `MeshOccludedUnit.fx`           |  ✓   |        —          |
| `MeshShadowVolume.fx`           |  ✓   |        —          |
| `MeshShield.fx`                 |  ✓   |        —          |
| `MeshSolidColor.fx`             |  ✓   |        —          |
| `Nebula.fx`                     |  ✓   |        —          |
| `Planet.fx`                     |  ✓   |        —          |
| `RSkinAdditive.fx`              |  ✓   |        —          |
| `RSkinAdditiveVColor.fx`        |  ✓   |        —          |
| `RSkinAlpha.fx`                 |  ✓   |        —          |
| `RSkinAlphaGloss.fx`            |  ✓   |        —          |
| `RSkinBumpColorize.fx`          |  ✓   |        —          |
| `RSkinBumpReflectColorize.fx`   |  ✓   |        —          |
| `RSkinGloss.fx`                 |  ✓   |        —          |
| `RSkinGlossColorize.fx`         |  ✓   |        —          |
| `RSkinHeat.fx`                  |  ✓   |        —          |
| `RSkinOccludedUnit.fx`          |  ✓   |        —          |
| `RSkinShadowVolume.fx`          |  ✓   |        —          |
| `Skydome.fx`                    |  ✓   |        —          |
| `TerrainMeshBump.fx`            |  ✓   |        —          |
| `TerrainMeshGloss.fx`           |  ✓   |        —          |
| `Tree.fx`                       |  ✓   |        —          |

The dialect was proven end-to-end on `alDefault.fx`: load in Max 2026 DirectX Shader material → apply to cube → export via `max2alamo` → open in AloViewer → renders with `Material: alDefault.fx` correctly resolved. All other stubs share the same Effects11 template (technique11 + `vs_5_0`/`ps_5_0` profiles + the same auto-bound WorldViewProjection / WorldInverseTranspose semantics) so they should load on the same basis, but each one needs an actual click-test before getting a ✓ in the second column.

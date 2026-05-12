# `shaders/max-preview/` — Effects11 stubs for 3ds Max 2026 authoring

These `.fx` files exist **only so 3ds Max 2026 can load a DirectX Shader material whose filename matches a Petroglyph stock shader, and surface the same parameter UI Petroglyph's shader did.** They are *not* shipped into EaW mods, and they have no effect on in-game rendering.

## Why this folder exists

Max 2026's DirectX Shader material runtime rejects Petroglyph's stock `.fx` files. The PG shaders are written for the DX9 effect framework (`fx_2_0` profile, inline render states, `VertexShader = (compiled_var)` assignment style). Max 2026's runtime expects Effects11 (`technique11`, state objects, `SetVertexShader(CompileShader(vs_5_0, ...))`). The two frameworks are not bridgeable by patching; the entire technique/pass structure must be rewritten.

Without these stubs, the canonical Petroglyph authoring workflow —

> Material Editor → DirectX Shader → load `MeshBumpColorize.fx` → parameter rollout appears → wire textures → preview → export

— breaks at "load." This folder fixes that by providing same-named Effects11 stubs that:

- have the same parameter names, types, and `UIName` annotations as the PG shaders
- render a simplified Blinn-Phong (or simpler) approximation in the Max viewport
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

For shaders without a stub yet, use the `Alamo_Shader_Name` node user-property override as a fallback. See [docs/development-log.md](../docs/development-log.md) for the stub roll-out plan.

## DO NOT

- **Do not** copy these `.fx` files into an EaW mod's `Data/Art/Shaders/` folder. The engine has its own real shaders by these names; replacing them with these stubs will visibly downgrade in-game rendering.
- **Do not** treat the stub's preview as authoritative. It's "close enough for authoring," not pixel-perfect parity with the engine.

## Status

| Shader                | Stub | Verified in Max 2026 |
|-----------------------|------|----------------------|
| `alDefault.fx`        | ✓    | _pending_            |
| `MeshBumpColorize.fx` | _planned_ | —              |
| `MeshAlpha.fx`        | _planned_ | —              |
| `MeshShadowVolume.fx` | _planned_ | —              |
| `MeshAdditive.fx`     | _planned_ | —              |
| `MeshCollision.fx`    | _planned_ | —              |
| `MeshGloss.fx`        | _planned_ | —              |

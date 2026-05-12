#!/usr/bin/env python3
"""
Generate Effects11 stub .fx files for shaders/max-preview/ from a manifest.

These stubs let 3ds Max 2026 load a DirectX Shader material whose filename
matches one of Petroglyph's stock Alamo shaders, and surface a parameter
rollout with the same parameter names PG uses. The stubs are Max-side only;
the exported .alo just references the filename, and EaW uses its own real
shaders at runtime. See shaders/max-preview/README.md.

Param-list source of truth, in order of precedence:
  1. Gaukler material_parameter_dict (what max2alamo actually writes to .alo)
  2. PG .fxh / .fx parameter declarations (UI surface, default values)

To add a new shader: append an entry to SHADERS below and re-run.
"""
from pathlib import Path

# ---------------------------------------------------------------------------
# Manifest. (shader_filename, render_mode, [params], optional _ALAMO_ markers)
#
# render_mode:
#   "lit-opaque"   - opaque, simple Lambert with optional BaseTexture/NormalTexture
#   "lit-alpha"    - alpha-blended (SRC_ALPHA / INV_SRC_ALPHA), with BaseTexture
#   "lit-additive" - additive blend (ONE / ONE)
#   "flat-color"   - opaque flat color (uses `Color` or `DebugColor` param)
#   "flat-alpha"   - alpha-blended flat color
#
# Each param is a tuple (type, name, default, kind) where:
#   type ::= "float" | "float2" | "float3" | "float4" | "tex"
#   kind ::= "color" | "scalar" | "vec" | "bitmap"  (drives annotations)
# ---------------------------------------------------------------------------

# Standard PG lit-material parameter block (used by many shaders).
COMMON_LIT = [
    ("float3", "Emissive",  (0, 0, 0),    "color"),
    ("float3", "Diffuse",   (1, 1, 1),    "color"),
    ("float3", "Specular",  (1, 1, 1),    "color"),
    ("float",  "Shininess", 32.0,         "scalar"),
]
COMMON_LIT_F4_DIFFUSE = [  # Alpha.fxh variants use float4 Diffuse
    ("float3", "Emissive",  (0, 0, 0),    "color"),
    ("float4", "Diffuse",   (1, 1, 1, 1), "color"),
    ("float3", "Specular",  (1, 1, 1),    "color"),
    ("float",  "Shininess", 32.0,         "scalar"),
]

# Texture entries.
TEX_BASE   = ("tex", "BaseTexture",        None, "bitmap")
TEX_NORMAL = ("tex", "NormalTexture",      None, "bitmap")
TEX_GLOSS  = ("tex", "GlossTexture",       None, "bitmap")
TEX_CLOUD  = ("tex", "CloudTexture",       None, "bitmap")
TEX_CLOUDN = ("tex", "CloudNormalTexture", None, "bitmap")
TEX_WAVE   = ("tex", "WaveTexture",        None, "bitmap")
TEX_DIST   = ("tex", "DistortionTexture",  None, "bitmap")
TEX_TEX0   = ("tex", "Texture0",           None, "bitmap")
TEX_TEX1   = ("tex", "Texture1",           None, "bitmap")
TEX_DETAIL = ("tex", "DetailTexture",      None, "bitmap")
TEX_NRMDET = ("tex", "NormalDetailTexture",None, "bitmap")

PARAM_COLORIZATION = ("float4", "Colorization", (0, 1, 0, 1), "color")
PARAM_UVOFFSET     = ("float4", "UVOffset",     (0, 0, 0, 0), "vec")
PARAM_UVSCROLL     = ("float2", "UVScrollRate", (0, 0),       "vec")

ALAMO_MESH        = {"phase": "Opaque",      "proc": "Mesh",        "vtype": "alD3dVertNU2",      "tangent": False, "shadow": False}
ALAMO_MESH_BUMP   = {"phase": "Opaque",      "proc": "Mesh",        "vtype": "alD3dVertNU2U3U3",  "tangent": True,  "shadow": False}
ALAMO_MESH_ALPHA  = {"phase": "Transparent", "proc": "Mesh",        "vtype": "alD3dVertNU2",      "tangent": False, "shadow": False}
ALAMO_SKIN        = {"phase": "Opaque",      "proc": "Skin",        "vtype": "alD3dVertRSkinNU2", "tangent": False, "shadow": False}
ALAMO_SKIN_BUMP   = {"phase": "Opaque",      "proc": "Skin",        "vtype": "alD3dVertRSkinNU2U3U3","tangent": True, "shadow": False}
ALAMO_SHADOWVOL_M = {"phase": "Shadow",      "proc": "ShadowVolume","vtype": "alD3dVertN",        "tangent": False, "shadow": True}
ALAMO_SHADOWVOL_S = {"phase": "Shadow",      "proc": "ShadowVolume","vtype": "alD3dVertRSkinN",   "tangent": False, "shadow": True}
ALAMO_GRASS       = {"phase": "Transparent", "proc": "Grass",       "vtype": "alD3dVertGrass",    "tangent": False, "shadow": False}

SHADERS = [
    # ---------- alDefault (already authored - skipped if it exists) ----------
    # ("alDefault.fx", "lit-opaque", [], ALAMO_MESH),

    # ---------- BumpColorize family ----------
    ("MeshBumpColorize.fx",   "lit-opaque", [
        *COMMON_LIT, PARAM_COLORIZATION, PARAM_UVOFFSET, TEX_BASE, TEX_NORMAL,
    ], ALAMO_MESH_BUMP),
    ("RSkinBumpColorize.fx",  "lit-opaque", [
        *COMMON_LIT, PARAM_COLORIZATION, PARAM_UVOFFSET, TEX_BASE, TEX_NORMAL,
    ], ALAMO_SKIN_BUMP),

    # ---------- BumpReflectColorize family ----------
    ("MeshBumpReflectColorize.fx",  "lit-opaque", [
        *COMMON_LIT, PARAM_COLORIZATION, PARAM_UVOFFSET, TEX_BASE, TEX_NORMAL,
    ], ALAMO_MESH_BUMP),
    ("RSkinBumpReflectColorize.fx", "lit-opaque", [
        *COMMON_LIT, PARAM_COLORIZATION, PARAM_UVOFFSET, TEX_BASE, TEX_NORMAL,
    ], ALAMO_SKIN_BUMP),

    # ---------- Gloss family ----------
    ("MeshGloss.fx",          "lit-opaque", [*COMMON_LIT, TEX_BASE], ALAMO_MESH),
    ("RSkinGloss.fx",         "lit-opaque", [*COMMON_LIT, TEX_BASE], ALAMO_SKIN),
    ("TerrainMeshGloss.fx",   "lit-opaque", [*COMMON_LIT, TEX_BASE], ALAMO_MESH),
    ("BatchMeshGloss.fx",     "lit-opaque", [*COMMON_LIT, TEX_BASE], ALAMO_MESH),

    # ---------- GlossColorize family ----------
    ("MeshGlossColorize.fx",  "lit-opaque", [
        *COMMON_LIT, PARAM_COLORIZATION, TEX_BASE, TEX_GLOSS,
    ], ALAMO_MESH),
    ("RSkinGlossColorize.fx", "lit-opaque", [
        *COMMON_LIT, PARAM_COLORIZATION, TEX_BASE, TEX_GLOSS,
    ], ALAMO_SKIN),

    # ---------- Alpha family ----------
    ("MeshAlpha.fx",          "lit-alpha", [*COMMON_LIT_F4_DIFFUSE, TEX_BASE], ALAMO_MESH_ALPHA),
    ("RSkinAlpha.fx",         "lit-alpha", [*COMMON_LIT_F4_DIFFUSE, TEX_BASE], ALAMO_SKIN),
    ("BatchMeshAlpha.fx",     "lit-alpha", [*COMMON_LIT_F4_DIFFUSE, TEX_BASE], ALAMO_MESH_ALPHA),
    ("MeshAlphaScroll.fx",    "lit-alpha", [
        *COMMON_LIT_F4_DIFFUSE, TEX_BASE, PARAM_UVSCROLL,
    ], ALAMO_MESH_ALPHA),

    # ---------- AlphaGloss family ----------
    ("MeshAlphaGloss.fx",     "lit-alpha", [
        *COMMON_LIT_F4_DIFFUSE, PARAM_COLORIZATION, TEX_BASE, TEX_GLOSS,
    ], ALAMO_MESH_ALPHA),
    ("RSkinAlphaGloss.fx",    "lit-alpha", [
        *COMMON_LIT_F4_DIFFUSE, PARAM_COLORIZATION, TEX_BASE, TEX_GLOSS,
    ], ALAMO_SKIN),

    # ---------- Additive family ----------
    ("MeshAdditive.fx",       "lit-additive", [
        TEX_BASE, PARAM_UVSCROLL,
        ("float4", "Color", (1, 1, 1, 1), "color"),
    ], ALAMO_MESH_ALPHA),
    ("RSkinAdditive.fx",      "lit-additive", [
        TEX_BASE, PARAM_UVSCROLL,
        ("float4", "Color", (1, 1, 1, 1), "color"),
    ], ALAMO_SKIN),
    ("MeshAdditiveVColor.fx", "lit-additive", [
        TEX_BASE, PARAM_UVSCROLL,
        ("float4", "Color", (1, 1, 1, 1), "color"),
    ], ALAMO_MESH_ALPHA),
    ("RSkinAdditiveVColor.fx","lit-additive", [
        TEX_BASE, PARAM_UVSCROLL,
        ("float4", "Color", (1, 1, 1, 1), "color"),
    ], ALAMO_SKIN),
    ("MeshAdditiveOffset.fx", "lit-additive", [TEX_BASE, PARAM_UVOFFSET], ALAMO_MESH_ALPHA),

    # ---------- Heat family ----------
    ("MeshHeat.fx",           "lit-additive", [TEX_DIST, PARAM_UVSCROLL], ALAMO_MESH_ALPHA),
    ("RSkinHeat.fx",          "lit-additive", [TEX_DIST, PARAM_UVSCROLL], ALAMO_SKIN),

    # ---------- Occluded family ----------
    ("MeshOccludedUnit.fx",   "flat-alpha", [
        ("float4", "Color", (1, 0, 0, 0.5), "color"),
    ], ALAMO_MESH_ALPHA),
    ("RSkinOccludedUnit.fx",  "flat-alpha", [
        ("float4", "Color", (1, 0, 0, 0.5), "color"),
    ], ALAMO_SKIN),

    # ---------- ShadowVolume ----------
    ("MeshShadowVolume.fx",   "flat-color", [
        ("float4", "DebugColor", (0, 1, 1, 1), "color"),
    ], ALAMO_SHADOWVOL_M),
    ("RSkinShadowVolume.fx",  "flat-color", [
        ("float4", "DebugColor", (0, 1, 1, 1), "color"),
    ], ALAMO_SHADOWVOL_S),

    # ---------- MeshShield ----------
    ("MeshShield.fx",         "lit-additive", [
        ("float4", "Color",                (1, 1, 1, 1), "color"),
        ("float",  "EdgeBrightness",       0.5,          "scalar"),
        ("float",  "BaseUVScale",          20.0,         "scalar"),
        ("float",  "WaveUVScale",          1.0,          "scalar"),
        ("float",  "DistortUVScale",       1.0,          "scalar"),
        ("float",  "BaseUVScrollRate",     1.0,          "scalar"),
        ("float",  "WaveUVScrollRate",     1.0,          "scalar"),
        ("float",  "DistortUVScrollRate",  1.0,          "scalar"),
        TEX_BASE, TEX_WAVE, TEX_DIST,
    ], ALAMO_MESH_ALPHA),

    # ---------- MeshCollision / MeshSolidColor ----------
    ("MeshCollision.fx",      "flat-color", [
        ("float4", "Color", (0, 0, 1, 0.5), "color"),
    ], ALAMO_MESH),
    ("MeshSolidColor.fx",     "flat-color", [
        ("float4", "Color", (0, 0, 1, 0.5), "color"),
    ], ALAMO_MESH),

    # ---------- MeshLightVisualize (no params) ----------
    ("MeshLightVisualize.fx", "lit-opaque", [], ALAMO_MESH),

    # ---------- BlobStencilMasked ----------
    ("BlobStencilMasked.fx",  "lit-alpha", [TEX_TEX0, TEX_TEX1], ALAMO_MESH_ALPHA),

    # ---------- Specialty ----------
    ("Grass.fx", "lit-alpha", [
        ("float3", "Emissive",  (0, 0, 0),    "color"),
        ("float4", "Diffuse",   (1, 1, 1, 1), "color"),
        ("float4", "Diffuse1",  (1, 1, 1, 1), "color"),
        ("float",  "BendScale", 1.0,          "scalar"),
        TEX_BASE,
    ], ALAMO_GRASS),
    ("Tree.fx", "lit-alpha", [
        *COMMON_LIT,
        ("float", "BendScale", 1.0, "scalar"),
        TEX_BASE, TEX_NORMAL,
    ], ALAMO_MESH_BUMP),
    ("TerrainMeshBump.fx", "lit-opaque", [
        *COMMON_LIT, TEX_BASE, TEX_NORMAL,
    ], ALAMO_MESH_BUMP),
    ("Planet.fx", "lit-opaque", [
        ("float3", "Emissive",        (0, 0, 0),    "color"),
        ("float3", "Diffuse",         (1, 1, 1),    "color"),
        ("float3", "Specular",        (1, 1, 1),    "color"),
        ("float4", "Atmosphere",      (0, 0, 0, 1), "color"),
        ("float3", "CityColor",       (1, 1, 1),    "color"),
        ("float",  "AtmospherePower", 4.5,          "scalar"),
        ("float",  "CloudScrollRate", 0.0025,       "scalar"),
        TEX_BASE, TEX_NORMAL, TEX_CLOUD, TEX_CLOUDN,
    ], ALAMO_MESH_BUMP),
    ("Nebula.fx", "lit-additive", [
        TEX_BASE,
        PARAM_UVSCROLL,
        ("float", "DistortionScale", 25.0,  "scalar"),
        ("float", "SFreq",           0.002, "scalar"),
        ("float", "TFreq",           0.05,  "scalar"),
    ], ALAMO_MESH_ALPHA),
    ("Skydome.fx", "lit-opaque", [
        ("float3", "Emissive",        (0, 0, 0), "color"),
        ("float",  "CloudScrollRate", 0.0025,    "scalar"),
        ("float",  "CloudScale",      0.0025,    "scalar"),
        TEX_BASE, TEX_CLOUD,
    ], ALAMO_MESH),
]


def fmt_default(typ, default):
    if default is None:
        return ""
    if typ == "float":
        return f" = {float(default)};"
    if isinstance(default, (tuple, list)):
        body = ", ".join(f"{float(x)}" for x in default)
        return f" = {{ {body} }};"
    return ""


def emit_param(p):
    """Emit a single DXSAS-style scalar/vector param declaration."""
    typ, name, default, kind = p
    if typ == "tex":
        # Effects11 requires Texture2D + paired SamplerState.
        return (
            f"Texture2D <float4> {name} <\n"
            f"    string UIName = \"{name}\";\n"
            f"    string UIType = \"bitmap\";\n"
            f"    string ResourceType = \"2D\";\n"
            f">;\n"
            f"SamplerState {name}Sampler {{\n"
            f"    MinFilter = Linear; MagFilter = Linear; MipFilter = Linear;\n"
            f"    AddressU = Wrap; AddressV = Wrap;\n"
            f"}};"
        )
    annot = [f'string UIName = "{name}";']
    if kind == "color":
        annot.append('string UIType = "ColorSwatch";')
        annot.append('string UIWidget = "Color";')
    return (
        f"{typ} {name} <\n    " + "\n    ".join(annot) + f"\n>{fmt_default(typ, default)}"
    )


def texture_list(params):
    return [p for p in params if p[0] == "tex"]


def has_param(params, name):
    return any(p[1] == name for p in params)


def emit_pixel_shader(render_mode, params):
    """Emit the body of the std_PS pixel shader."""
    texs = texture_list(params)
    # Pick the primary diffuse texture if any.
    primary_tex = None
    for cand in ("BaseTexture", "Texture0", "DistortionTexture"):
        if any(p[1] == cand for p in texs):
            primary_tex = cand
            break

    sample_line = (
        f"    float4 base = {primary_tex}.Sample({primary_tex}Sampler, IN.UV);"
        if primary_tex else
        "    float4 base = float4(0.7, 0.7, 0.7, 1.0);"
    )

    if render_mode == "flat-color":
        # Use DebugColor or Color literally.
        if has_param(params, "DebugColor"):
            return "    return DebugColor;"
        if has_param(params, "Color"):
            return "    return float4(Color.rgb, 1.0);"
        return "    return float4(1, 0, 1, 1);"

    if render_mode == "flat-alpha":
        if has_param(params, "Color"):
            return "    return Color;"
        return "    return float4(1, 0, 1, 0.5);"

    # Tint by Diffuse / Color when present.
    tint = "float3 tint = float3(1,1,1);"
    if has_param(params, "Diffuse"):
        # Diffuse may be float3 or float4 — both have .rgb
        tint = "float3 tint = Diffuse.rgb;"
    elif has_param(params, "Color"):
        tint = "float3 tint = Color.rgb;"

    emissive_add = ""
    if has_param(params, "Emissive"):
        emissive_add = " + Emissive"

    if render_mode == "lit-opaque":
        return (
            f"{sample_line}\n"
            f"    {tint}\n"
            f"    const float3 keyDir = normalize(float3(0.3, 0.6, 0.7));\n"
            f"    float ndl = saturate(dot(normalize(IN.WorldNrm), keyDir));\n"
            f"    float3 col = base.rgb * tint * (0.25 + 0.75 * ndl){emissive_add};\n"
            f"    return float4(col, 1.0);"
        )

    if render_mode == "lit-alpha":
        return (
            f"{sample_line}\n"
            f"    {tint}\n"
            f"    const float3 keyDir = normalize(float3(0.3, 0.6, 0.7));\n"
            f"    float ndl = saturate(dot(normalize(IN.WorldNrm), keyDir));\n"
            f"    float3 col = base.rgb * tint * (0.25 + 0.75 * ndl){emissive_add};\n"
            f"    return float4(col, base.a);"
        )

    if render_mode == "lit-additive":
        return (
            f"{sample_line}\n"
            f"    {tint}\n"
            f"    return float4(base.rgb * tint, base.a);"
        )

    raise ValueError(f"Unknown render mode: {render_mode}")


def emit_blend_state(render_mode):
    if render_mode == "lit-opaque":
        return None
    if render_mode == "flat-color":
        return None
    if render_mode == "lit-alpha" or render_mode == "flat-alpha":
        return (
            "BlendState AlphaBlend {\n"
            "    BlendEnable[0] = TRUE;\n"
            "    SrcBlend = SRC_ALPHA;\n"
            "    DestBlend = INV_SRC_ALPHA;\n"
            "    BlendOp = ADD;\n"
            "    SrcBlendAlpha = ONE;\n"
            "    DestBlendAlpha = ZERO;\n"
            "    BlendOpAlpha = ADD;\n"
            "};\n"
            "DepthStencilState NoZWrite {\n"
            "    DepthEnable = TRUE;\n"
            "    DepthWriteMask = ZERO;\n"
            "};\n"
        ), "AlphaBlend", "NoZWrite"
    if render_mode == "lit-additive":
        return (
            "BlendState AdditiveBlend {\n"
            "    BlendEnable[0] = TRUE;\n"
            "    SrcBlend = ONE;\n"
            "    DestBlend = ONE;\n"
            "    BlendOp = ADD;\n"
            "};\n"
            "DepthStencilState NoZWrite {\n"
            "    DepthEnable = TRUE;\n"
            "    DepthWriteMask = ZERO;\n"
            "};\n"
        ), "AdditiveBlend", "NoZWrite"
    raise ValueError(f"Unknown render mode: {render_mode}")


HEADER = """// max2alamo-2026 :: shaders/max-preview/{name}
//
// Effects11 STUB shader for 3ds Max 2026 authoring preview.
// Mirrors the parameter surface of Petroglyph's stock {name} for Max-side UI.
// This shader does NOT ship into EaW mods. See ../README.md.
//
// Auto-generated by scripts/generate-max-preview-stubs.py.
// Edit the manifest in that file and re-run to regenerate.
"""


def alamo_block(markers):
    return (
        f'string _ALAMO_RENDER_PHASE = "{markers["phase"]}";\n'
        f'string _ALAMO_VERTEX_PROC  = "{markers["proc"]}";\n'
        f'string _ALAMO_VERTEX_TYPE  = "{markers["vtype"]}";\n'
        f'bool   _ALAMO_TANGENT_SPACE = {str(markers["tangent"]).lower()};\n'
        f'bool   _ALAMO_SHADOW_VOLUME = {str(markers["shadow"]).lower()};\n'
    )


def emit_stub(name, render_mode, params, markers):
    out = [HEADER.format(name=name), ""]
    out.append(alamo_block(markers))
    out.append("")
    # Auto-bound transforms.
    out.append('float4x4 WvpXf     : WorldViewProjection    < string UIWidget = "None"; >;')
    out.append('float4x4 WorldITXf : WorldInverseTranspose  < string UIWidget = "None"; >;')
    out.append("")
    # Material params.
    for p in params:
        out.append(emit_param(p))
        out.append("")

    # Optional blend state.
    blend = emit_blend_state(render_mode)
    blend_name = depth_name = None
    if blend is not None:
        out.append(blend[0])
        _, blend_name, depth_name = blend

    # Vertex IO.
    needs_uv = any(p[0] == "tex" for p in params)
    out.append("struct VsIn {")
    out.append("    float4 Position : POSITION;")
    out.append("    float3 Normal   : NORMAL;")
    if needs_uv:
        out.append("    float2 Tex      : TEXCOORD0;")
    out.append("};")
    out.append("")
    out.append("struct VsOut {")
    out.append("    float4 HPosition : SV_Position;")
    out.append("    float3 WorldNrm  : TEXCOORD0;")
    if needs_uv:
        out.append("    float2 UV        : TEXCOORD1;")
    out.append("};")
    out.append("")
    # VS.
    out.append("VsOut std_VS(VsIn IN) {")
    out.append("    VsOut OUT = (VsOut)0;")
    out.append("    OUT.HPosition = mul(IN.Position, WvpXf);")
    out.append("    OUT.WorldNrm  = mul(IN.Normal, (float3x3)WorldITXf);")
    if needs_uv:
        out.append("    OUT.UV        = IN.Tex;")
    out.append("    return OUT;")
    out.append("}")
    out.append("")
    # PS.
    out.append("float4 std_PS(VsOut IN) : SV_Target {")
    out.append(emit_pixel_shader(render_mode, params))
    out.append("}")
    out.append("")
    # Technique.
    out.append("technique11 Main {")
    out.append("    pass P0 {")
    if blend_name is not None:
        out.append(f"        SetBlendState({blend_name}, float4(0,0,0,0), 0xffffffff);")
        out.append(f"        SetDepthStencilState({depth_name}, 0);")
    out.append("        SetVertexShader  (CompileShader(vs_5_0, std_VS()));")
    out.append("        SetGeometryShader(NULL);")
    out.append("        SetPixelShader   (CompileShader(ps_5_0, std_PS()));")
    out.append("    }")
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    repo_root = Path(__file__).resolve().parent.parent
    out_dir = repo_root / "shaders" / "max-preview"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Writing to: {out_dir}")
    count = 0
    for name, mode, params, markers in SHADERS:
        path = out_dir / name
        path.write_text(emit_stub(name, mode, params, markers), encoding="utf-8")
        count += 1
        print(f"  + {name}")
    print(f"Done. {count} stubs written.")


if __name__ == "__main__":
    main()

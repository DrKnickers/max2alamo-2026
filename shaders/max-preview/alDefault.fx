// max2alamo-2026 :: shaders/max-preview/alDefault.fx
//
// Effects11 STUB shader for 3ds Max 2026 authoring preview.
// MIRRORS THE PARAMETER SURFACE OF PETROGLYPH'S alDefault.fx FOR MAX-SIDE UI.
// THIS SHADER DOES NOT SHIP INTO EAW MODS. See ../README.md.
//
// Petroglyph's vanilla alDefault is parameterless from the .alo POV
// (Gaukler material_parameter_dict["alDefault.fx"] is empty). So this
// stub exposes no DXSAS parameters either -- it just renders the
// surface with Max's lighting so the user can confirm "yes, alDefault
// is selected" in the viewport. Texturing is left to the standard
// Max material slot wiring or per-mesh BaseTexture user properties.

string _ALAMO_RENDER_PHASE = "Opaque";
string _ALAMO_VERTEX_PROC  = "Mesh";
string _ALAMO_VERTEX_TYPE  = "alD3dVertNU2";
bool   _ALAMO_TANGENT_SPACE = false;
bool   _ALAMO_SHADOW_VOLUME = false;

// Auto-bound transforms (Max fills these via the DXSAS semantic).
float4x4 WvpXf      : WorldViewProjection < string UIWidget = "None"; >;
float4x4 WorldITXf  : WorldInverseTranspose < string UIWidget = "None"; >;

struct VsIn
{
    float4 Position : POSITION;
    float3 Normal   : NORMAL;
};

struct VsOut
{
    float4 HPosition : SV_Position;
    float3 WorldNrm  : TEXCOORD0;
};

VsOut std_VS(VsIn IN)
{
    VsOut OUT = (VsOut)0;
    OUT.HPosition = mul(IN.Position, WvpXf);
    OUT.WorldNrm  = normalize(mul(IN.Normal, (float3x3)WorldITXf));
    return OUT;
}

float4 std_PS(VsOut IN) : SV_Target
{
    // Fixed key light from above-forward so the user sees shading.
    const float3 keyDir = normalize(float3(0.3, 0.6, 0.7));
    float ndl = saturate(dot(normalize(IN.WorldNrm), keyDir));
    float3 col = float3(0.7, 0.7, 0.7) * (0.25 + 0.75 * ndl);
    return float4(col, 1.0);
}

technique11 Main
{
    pass P0
    {
        SetVertexShader  (CompileShader(vs_5_0, std_VS()));
        SetGeometryShader(NULL);
        SetPixelShader   (CompileShader(ps_5_0, std_PS()));
    }
}

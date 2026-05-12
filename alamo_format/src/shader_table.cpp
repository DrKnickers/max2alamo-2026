#include "alamo_format/shader_table.h"

#include <unordered_map>

namespace alamo_format::shader_table {

namespace {

using Kind = MaterialParam::Kind;

// Convenience helpers so the table reads cleanly.
//
// V()  declares a float3 param: the on-disk chunk is still a 16-byte FLOAT4
//      but the 4th slot is forced to 0 at write time to match vanilla.
// V4() declares a genuine float4 param (Colorization, UVOffset, Color,
//      DebugColor) where the 4th slot carries actual data.
constexpr ParamSpec F (std::string_view n, float a)                              { return {n, Kind::Float,   {a, 0, 0, 0}, false}; }
constexpr ParamSpec V (std::string_view n, float a, float b, float c)            { return {n, Kind::Float4,  {a, b, c, 0}, true};  }
constexpr ParamSpec V4(std::string_view n, float a, float b, float c, float d)   { return {n, Kind::Float4,  {a, b, c, d}, false}; }
constexpr ParamSpec T (std::string_view n)                                       { return {n, Kind::Texture, {0, 0, 0, 0}, false}; }

// Per-shader param templates. Defaults come from the PG .fxh headers;
// vanilla content writes 4-element vectors even for parameters declared
// `float3` in HLSL (Emissive, Diffuse, etc.) -- the 4th element is 0.

// alDefault has zero params; the map uses a default-constructed ParamSpecList for it.
constexpr ParamSpec kBatchMeshAlpha[]     = { V("Emissive",0,0,0), V4("Diffuse",1,1,1,1), V("Specular",1,1,1), F("Shininess",32), T("BaseTexture") };
constexpr ParamSpec kBatchMeshGloss[]     = { V("Emissive",0,0,0), V("Diffuse",1,1,1),    V("Specular",1,1,1), F("Shininess",32), T("BaseTexture") };
constexpr ParamSpec kGrass[]              = { V("Emissive",0,0,0), V4("Diffuse",1,1,1,1), V4("Diffuse1",1,1,1,1), F("BendScale",1), T("BaseTexture") };
constexpr ParamSpec kMeshAdditive[]       = { T("BaseTexture"), V("UVScrollRate",0,0,0), V4("Color",1,1,1,1) };
constexpr ParamSpec kMeshAlpha[]          = { V("Emissive",0,0,0), V4("Diffuse",1,1,1,1), V("Specular",1,1,1), F("Shininess",32), T("BaseTexture") };
constexpr ParamSpec kMeshAlphaScroll[]    = { V("Emissive",0,0,0), V4("Diffuse",1,1,1,1), V("Specular",1,1,1), F("Shininess",32), T("BaseTexture") };
constexpr ParamSpec kMeshBumpColorize[]   = {
    V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32),
    V4("Colorization",0,1,0,1), V4("UVOffset",0,0,0,0),
    T("BaseTexture"), T("NormalTexture"),
};
constexpr ParamSpec kMeshCollision[]      = { V4("Color", 0,0,1,0.5f) };
constexpr ParamSpec kMeshGloss[]          = { V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32), T("BaseTexture") };
constexpr ParamSpec kMeshGlossColorize[]  = { V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32),
                                              T("BaseTexture"), T("GlossTexture") };
constexpr ParamSpec kMeshShadowVolume[]   = { V4("DebugColor", 0,1,1,1) };
constexpr ParamSpec kMeshShield[]         = {
    V4("Color",1,1,1,1),
    F("EdgeBrightness",0.5f),
    F("BaseUVScale",20.0f), F("WaveUVScale",1.0f), F("DistortUVScale",1.0f),
    F("BaseUVScrollRate",1.0f), F("WaveUVScrollRate",1.0f), F("DistortUVScrollRate",1.0f),
    T("BaseTexture"), T("WaveTexture"), T("DistortionTexture"),
};
constexpr ParamSpec kNebula[]             = {
    T("BaseTexture"), V("UVScrollRate",0,0,0),
    F("DistortionScale",25.0f), F("SFreq",0.002f), F("TFreq",0.05f),
};
constexpr ParamSpec kPlanet[]             = {
    V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1),
    V4("Atmosphere",0,0,0,1), V("CityColor",1,1,1),
    F("AtmospherePower",4.5f), F("CloudScrollRate",0.0025f),
    T("BaseTexture"), T("NormalTexture"), T("CloudTexture"), T("CloudNormalTexture"),
};
constexpr ParamSpec kRSkinAdditive[]      = { T("BaseTexture"), V("UVScrollRate",0,0,0), V4("Color",1,1,1,1) };
constexpr ParamSpec kRSkinAlpha[]         = { V("Emissive",0,0,0), V4("Diffuse",1,1,1,1), V("Specular",1,1,1), F("Shininess",32), T("BaseTexture") };
constexpr ParamSpec kRSkinBumpColorize[]  = {
    V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32),
    V4("Colorization",0,1,0,1), V4("UVOffset",0,0,0,0),
    T("BaseTexture"), T("NormalTexture"),
};
constexpr ParamSpec kRSkinGloss[]         = { V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32), T("BaseTexture") };
constexpr ParamSpec kRSkinGlossColorize[] = {
    V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32),
    V4("Colorization",0,1,0,1), T("BaseTexture"), T("GlossTexture"),
};
constexpr ParamSpec kRSkinShadowVolume[]  = { V4("DebugColor", 0,1,1,1) };
constexpr ParamSpec kSkydome[]            = {
    V("Emissive",0,0,0),
    F("CloudScrollRate",0.0025f), F("CloudScale",0.0025f),
    T("BaseTexture"), T("CloudTexture"),
};
constexpr ParamSpec kTerrainMeshBump[]    = { V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32), T("BaseTexture"), T("NormalTexture") };
constexpr ParamSpec kTerrainMeshGloss[]   = { V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32), T("BaseTexture") };
constexpr ParamSpec kTree[]               = {
    V("Emissive",0,0,0), V("Diffuse",1,1,1), V("Specular",1,1,1), F("Shininess",32),
    F("BendScale",1), T("BaseTexture"), T("NormalTexture"),
};

#define ENTRY(name, array) { name, ParamSpecList{ array, sizeof(array) / sizeof(ParamSpec) } }

// alDefault has zero params -- emit just shader_name + BaseTexture
// (texture is per-mesh, not in the param block). The empty array case is
// handled by ParamSpecList default-constructed to {nullptr, 0}.
constexpr ParamSpecList kAlDefaultEmpty{};

const std::unordered_map<std::string_view, ParamSpecList>& table() {
    static const std::unordered_map<std::string_view, ParamSpecList> t = {
        {"alDefault.fx",              kAlDefaultEmpty                                                  },
        ENTRY("BatchMeshAlpha.fx",     kBatchMeshAlpha    ),
        ENTRY("BatchMeshGloss.fx",     kBatchMeshGloss    ),
        ENTRY("Grass.fx",              kGrass             ),
        ENTRY("MeshAdditive.fx",       kMeshAdditive      ),
        ENTRY("MeshAlpha.fx",          kMeshAlpha         ),
        ENTRY("MeshAlphaScroll.fx",    kMeshAlphaScroll   ),
        ENTRY("MeshBumpColorize.fx",   kMeshBumpColorize  ),
        ENTRY("MeshCollision.fx",      kMeshCollision     ),
        ENTRY("MeshGloss.fx",          kMeshGloss         ),
        ENTRY("MeshGlossColorize.fx",  kMeshGlossColorize ),
        ENTRY("MeshShadowVolume.fx",   kMeshShadowVolume  ),
        ENTRY("MeshShield.fx",         kMeshShield        ),
        ENTRY("Nebula.fx",             kNebula            ),
        ENTRY("Planet.fx",             kPlanet            ),
        ENTRY("RSkinAdditive.fx",      kRSkinAdditive     ),
        ENTRY("RSkinAlpha.fx",         kRSkinAlpha        ),
        ENTRY("RSkinBumpColorize.fx",  kRSkinBumpColorize ),
        ENTRY("RSkinGloss.fx",         kRSkinGloss        ),
        ENTRY("RSkinGlossColorize.fx", kRSkinGlossColorize),
        ENTRY("RSkinShadowVolume.fx",  kRSkinShadowVolume ),
        ENTRY("Skydome.fx",            kSkydome           ),
        ENTRY("TerrainMeshBump.fx",    kTerrainMeshBump   ),
        ENTRY("TerrainMeshGloss.fx",   kTerrainMeshGloss  ),
        ENTRY("Tree.fx",               kTree              ),
    };
    return t;
}

#undef ENTRY

}  // namespace

ParamSpecList params_for(std::string_view shader_filename) {
    const auto& t = table();
    auto it = t.find(shader_filename);
    return (it == t.end()) ? ParamSpecList{} : it->second;
}

bool contains(std::string_view shader_filename) {
    return table().find(shader_filename) != table().end();
}

}  // namespace alamo_format::shader_table

#include "alamo_format/vertex_format_selector.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace alamo_format::vertex_format_selector {

namespace {

// Case-insensitive string compare. AloViewer's `VertexManager::
// GetVertexFormat` uses `_stricmp` on the 0x10002 payload against
// its name table, so we match that convention here. Same semantics
// for shader-name lookup (Max returns mixed-case material->mtlEffect
// filenames; we don't want to be fragile to case differences in
// modder-shipped scenes).
bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

struct Entry {
    std::string_view shader;
    std::string_view vfmt;
};

// Shader → vertex-format-name mapping. Source: the dominant vanilla
// pairing per `scripts/survey_vertex_formats.py` survey of 10,737
// vanilla EaW + FoC submeshes (60 unique triplets, 35 distinct
// shaders in active use, 10 of AloViewer's 15-name set represented).
// For shaders absent from the corpus we infer from naming convention
// + the `_ALAMO_VERTEX_TYPE` directives in our own stub `.fx` files
// (`shaders/max-preview/`), preferring the corpus signal when both
// disagree.
constexpr std::array<Entry, 42> kStockShaderTable = {{
    // Corpus-empirical (most common pairing per shader)
    {"alDefault.fx",                  "alD3dVertNU2"},
    {"BatchMeshAlpha.fx",             "alD3dVertNU2"},
    {"BatchMeshGloss.fx",             "alD3dVertNU2"},
    {"Default.fx",                    "alD3dVertN"},
    {"FissureMeshGloss.fx",           "alD3dVertNU2"},
    {"Grass.fx",                      "alD3dVertGrass"},
    {"MeshAdditive.fx",               "alD3dVertNU2"},
    {"MeshAdditiveOffset.fx",         "alD3dVertNU2"},
    {"MeshAdditiveReflection.fx",     "alD3dVertNU2C"},
    {"MeshAdditiveVColor.fx",         "alD3dVertNU2C"},
    {"MeshAlpha.fx",                  "alD3dVertNU2"},
    {"MeshAlphaGloss.fx",             "alD3dVertNU2"},
    {"MeshAlphaScroll.fx",            "alD3dVertNU2"},
    {"MeshBumpColorize.fx",           "alD3dVertNU2U3U3"},
    {"MeshCollision.fx",              "alD3dVertN"},
    {"MeshGloss.fx",                  "alD3dVertNU2"},
    {"MeshGlossColorize.fx",          "alD3dVertNU2"},
    {"MeshHeat.fx",                   "alD3dVertNU2C"},
    {"MeshLightVisualize.fx",         "alD3dVertNU2"},
    {"MeshShadowVolume.fx",           "alD3dVertN"},
    {"MeshShield.fx",                 "alD3dVertNU2C"},
    {"MeshSolidColor.fx",             "alD3dVertN"},
    {"Nebula.fx",                     "alD3dVertNU2C"},
    {"Planet.fx",                     "alD3dVertNU2U3U3"},
    {"RSkinAdditive.fx",              "alD3dVertRSkinNU2"},
    {"RSkinAlpha.fx",                 "alD3dVertRSkinNU2"},
    {"RSkinAlphaGloss.fx",            "alD3dVertRSkinNU2"},
    {"RSkinBumpColorize.fx",          "alD3dVertRSkinNU2U3U3"},
    {"RSkinGloss.fx",                 "alD3dVertRSkinNU2"},
    {"RSkinGlossColorize.fx",         "alD3dVertRSkinNU2"},
    {"RSkinShadowVolume.fx",          "alD3dVertRSkinNU2"},
    {"Skydome.fx",                    "alD3dVertNU2C"},
    {"TerrainMeshBump.fx",            "alD3dVertNU2U3U3"},
    {"TerrainMeshGloss.fx",           "alD3dVertNU2"},
    {"Tree.fx",                       "alD3dVertNU2"},
    // Inferred-from-naming (not in corpus tally but listed in our
    // stub-shader manifest — modders may use these even if vanilla
    // shipped no instances).
    {"BlobStencilMasked.fx",          "alD3dVertNU2"},
    {"MeshBumpReflectColorize.fx",    "alD3dVertNU2U3U3"},
    {"MeshOccludedUnit.fx",           "alD3dVertNU2"},
    {"RSkinAdditiveVColor.fx",        "alD3dVertRSkinNU2C"},
    {"RSkinBumpReflectColorize.fx",   "alD3dVertRSkinNU2U3U3"},
    {"RSkinHeat.fx",                  "alD3dVertRSkinNU2C"},
    {"RSkinOccludedUnit.fx",          "alD3dVertRSkinNU2"},
}};

// AloViewer's recognized vertex-format-name table — the closed set of
// 15 strings its renderer's `VertexManager::GetVertexFormat` accepts.
// Source: github.com/GlyphXTools/alo-viewer at
// `src/RenderEngine/DirectX9/VertexFormats.cpp`'s `VertexFormatNames[]`
// array. Strings here are mixed-case for documentation; comparison is
// case-insensitive per AloViewer's `_stricmp` semantics.
constexpr std::array<std::string_view, 15> kRecognizedVertexFormats = {
    "alD3dVertN",
    "alD3dVertNU2",
    "alD3dVertNU2C",
    "alD3dVertNU2U3U3",
    "alD3dVertNU2U3U3C",
    "alD3dVertRSkinNU2",
    "alD3dVertRSkinNU2C",
    "alD3dVertRSkinNU2U3U3",
    "alD3dVertRSkinNU2U3U3C",
    "alD3dVertB4I4NU2",
    "alD3dVertB4I4NU2C",
    "alD3dVertB4I4NU2U3U3",
    "alD3dVertB4I4NU2U3U3C",
    "alD3dVertGrass",
    "alD3dVertBillboard",
};

}  // namespace

std::string default_vertex_format_for_shader(std::string_view shader_name) {
    for (const auto& e : kStockShaderTable) {
        if (iequal(shader_name, e.shader)) {
            return std::string(e.vfmt);
        }
    }
    return {};
}

bool is_recognized_vertex_format(std::string_view name) {
    for (const auto& v : kRecognizedVertexFormats) {
        if (iequal(name, v)) return true;
    }
    return false;
}

}  // namespace alamo_format::vertex_format_selector

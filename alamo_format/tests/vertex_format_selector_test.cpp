// Catch2 tests for Phase 10's vertex-format selector + AloViewer
// recognized-format-name validator (issue #75).
//
// The shader→format table is the writer's source of truth for which
// `0x10002` string to emit per submesh; the recognizer is the
// guardrail that catches typos in the per-mesh override path (PR C).

#include <catch2/catch_test_macros.hpp>

#include "alamo_format/vertex_format_selector.h"

using alamo_format::vertex_format_selector::default_vertex_format_for_shader;
using alamo_format::vertex_format_selector::is_recognized_vertex_format;

TEST_CASE("default_vertex_format_for_shader: corpus-empirical mappings", "[vertex_format_selector]") {
    // Top corpus shaders -- each should resolve to its dominant vanilla pairing.
    SECTION("alDefault → alD3dVertNU2") {
        REQUIRE(default_vertex_format_for_shader("alDefault.fx") == "alD3dVertNU2");
    }
    SECTION("MeshBumpColorize → alD3dVertNU2U3U3") {
        REQUIRE(default_vertex_format_for_shader("MeshBumpColorize.fx") == "alD3dVertNU2U3U3");
    }
    SECTION("MeshShadowVolume → alD3dVertN (no UV)") {
        REQUIRE(default_vertex_format_for_shader("MeshShadowVolume.fx") == "alD3dVertN");
    }
    SECTION("MeshCollision → alD3dVertN (no UV)") {
        REQUIRE(default_vertex_format_for_shader("MeshCollision.fx") == "alD3dVertN");
    }
    SECTION("RSkinBumpColorize → alD3dVertRSkinNU2U3U3") {
        REQUIRE(default_vertex_format_for_shader("RSkinBumpColorize.fx") == "alD3dVertRSkinNU2U3U3");
    }
    SECTION("RSkinGlossColorize → alD3dVertRSkinNU2 (Snowtrooper bug)") {
        // The exact mapping that fixed the user's broken Snowtrooper
        // export -- skinned data was previously tagged alD3dVertNU2,
        // causing the renderer to bind the static-mesh declaration
        // and produce stretched-triangle artifacts.
        REQUIRE(default_vertex_format_for_shader("RSkinGlossColorize.fx") == "alD3dVertRSkinNU2");
    }
    SECTION("RSkinShadowVolume → alD3dVertRSkinNU2 (NOT the stub-script's RSkinN)") {
        // The stub-shader manifest at scripts/generate-max-preview-stubs.py
        // declares `alD3dVertRSkinN` for RSkinShadowVolume but vanilla
        // corpus has 196/196 submeshes using `alD3dVertRSkinNU2`. Corpus
        // wins -- corroborated by 0 vanilla observations of the alleged
        // RSkinN form for this shader.
        REQUIRE(default_vertex_format_for_shader("RSkinShadowVolume.fx") == "alD3dVertRSkinNU2");
    }
    SECTION("Grass → alD3dVertGrass") {
        REQUIRE(default_vertex_format_for_shader("Grass.fx") == "alD3dVertGrass");
    }
    SECTION("MeshAdditiveVColor → alD3dVertNU2C (vcolor)") {
        REQUIRE(default_vertex_format_for_shader("MeshAdditiveVColor.fx") == "alD3dVertNU2C");
    }
}

TEST_CASE("default_vertex_format_for_shader: inferred (corpus-absent) mappings", "[vertex_format_selector]") {
    // Shaders in our stub manifest but not in the corpus tally -- still
    // need to resolve to sensible vertex formats so modders authoring
    // with them get rendering that works.
    SECTION("BlobStencilMasked → alD3dVertNU2") {
        REQUIRE(default_vertex_format_for_shader("BlobStencilMasked.fx") == "alD3dVertNU2");
    }
    SECTION("MeshBumpReflectColorize → alD3dVertNU2U3U3") {
        REQUIRE(default_vertex_format_for_shader("MeshBumpReflectColorize.fx") == "alD3dVertNU2U3U3");
    }
    SECTION("RSkinAdditiveVColor → alD3dVertRSkinNU2C") {
        REQUIRE(default_vertex_format_for_shader("RSkinAdditiveVColor.fx") == "alD3dVertRSkinNU2C");
    }
    SECTION("RSkinHeat → alD3dVertRSkinNU2C") {
        REQUIRE(default_vertex_format_for_shader("RSkinHeat.fx") == "alD3dVertRSkinNU2C");
    }
}

TEST_CASE("default_vertex_format_for_shader: unknown shader returns empty", "[vertex_format_selector]") {
    SECTION("custom mod shader") {
        REQUIRE(default_vertex_format_for_shader("CustomModShader.fx").empty());
    }
    SECTION("empty input") {
        REQUIRE(default_vertex_format_for_shader("").empty());
    }
    SECTION("typo / wrong extension") {
        REQUIRE(default_vertex_format_for_shader("MeshAlpha.FX").empty() == false);  // case-insens
        REQUIRE(default_vertex_format_for_shader("MeshAlpha").empty());              // no .fx suffix
    }
}

TEST_CASE("default_vertex_format_for_shader: case-insensitive match", "[vertex_format_selector]") {
    // Max returns shader filenames in whatever case the modder authored;
    // we shouldn't reject "rskingloss.fx" or "RSKINGLOSS.FX" as unknown.
    REQUIRE(default_vertex_format_for_shader("rskingloss.fx") == "alD3dVertRSkinNU2");
    REQUIRE(default_vertex_format_for_shader("RSKINGLOSS.FX") == "alD3dVertRSkinNU2");
    REQUIRE(default_vertex_format_for_shader("MeshAlpha.fx") == "alD3dVertNU2");
}

TEST_CASE("is_recognized_vertex_format: AloViewer 15-entry table", "[vertex_format_selector]") {
    // Every name in AloViewer's VertexFormatNames table should be
    // recognized.
    REQUIRE(is_recognized_vertex_format("alD3dVertN"));
    REQUIRE(is_recognized_vertex_format("alD3dVertNU2"));
    REQUIRE(is_recognized_vertex_format("alD3dVertNU2C"));
    REQUIRE(is_recognized_vertex_format("alD3dVertNU2U3U3"));
    REQUIRE(is_recognized_vertex_format("alD3dVertNU2U3U3C"));
    REQUIRE(is_recognized_vertex_format("alD3dVertRSkinNU2"));
    REQUIRE(is_recognized_vertex_format("alD3dVertRSkinNU2C"));
    REQUIRE(is_recognized_vertex_format("alD3dVertRSkinNU2U3U3"));
    REQUIRE(is_recognized_vertex_format("alD3dVertRSkinNU2U3U3C"));
    REQUIRE(is_recognized_vertex_format("alD3dVertB4I4NU2"));
    REQUIRE(is_recognized_vertex_format("alD3dVertB4I4NU2C"));
    REQUIRE(is_recognized_vertex_format("alD3dVertB4I4NU2U3U3"));
    REQUIRE(is_recognized_vertex_format("alD3dVertB4I4NU2U3U3C"));
    REQUIRE(is_recognized_vertex_format("alD3dVertGrass"));
    REQUIRE(is_recognized_vertex_format("alD3dVertBillboard"));
}

TEST_CASE("is_recognized_vertex_format: case-insensitive (AloViewer uses _stricmp)", "[vertex_format_selector]") {
    REQUIRE(is_recognized_vertex_format("ALD3DVERTNU2"));
    REQUIRE(is_recognized_vertex_format("ald3dvertnu2"));
    REQUIRE(is_recognized_vertex_format("aLd3DvErTrSkInNu2"));
}

TEST_CASE("is_recognized_vertex_format: rejects unknown / typos", "[vertex_format_selector]") {
    REQUIRE_FALSE(is_recognized_vertex_format(""));
    REQUIRE_FALSE(is_recognized_vertex_format("alD3dVertNU2X"));
    REQUIRE_FALSE(is_recognized_vertex_format("alD3dVertNU3"));      // typo
    REQUIRE_FALSE(is_recognized_vertex_format("alD3dVertRSkin"));    // truncated
    REQUIRE_FALSE(is_recognized_vertex_format("D3dVertNU2"));        // missing al prefix
    REQUIRE_FALSE(is_recognized_vertex_format("alD3dVertB4I4"));     // truncated
    REQUIRE_FALSE(is_recognized_vertex_format("MeshAlpha.fx"));      // shader name, not format
}

TEST_CASE("table coverage: every shader resolves to a recognized format", "[vertex_format_selector]") {
    // Every entry in the shader→format table should map to a name
    // recognized by AloViewer. Catches the case where a table edit
    // introduces a typo or invents a name AloViewer doesn't know.
    constexpr const char* kKnownShaders[] = {
        "alDefault.fx", "BatchMeshAlpha.fx", "BatchMeshGloss.fx",
        "BlobStencilMasked.fx", "Default.fx", "FissureMeshGloss.fx",
        "Grass.fx", "MeshAdditive.fx", "MeshAdditiveOffset.fx",
        "MeshAdditiveReflection.fx", "MeshAdditiveVColor.fx",
        "MeshAlpha.fx", "MeshAlphaGloss.fx", "MeshAlphaScroll.fx",
        "MeshBumpColorize.fx", "MeshBumpReflectColorize.fx",
        "MeshCollision.fx", "MeshGloss.fx", "MeshGlossColorize.fx",
        "MeshHeat.fx", "MeshLightVisualize.fx", "MeshOccludedUnit.fx",
        "MeshShadowVolume.fx", "MeshShield.fx", "MeshSolidColor.fx",
        "Nebula.fx", "Planet.fx", "RSkinAdditive.fx",
        "RSkinAdditiveVColor.fx", "RSkinAlpha.fx", "RSkinAlphaGloss.fx",
        "RSkinBumpColorize.fx", "RSkinBumpReflectColorize.fx",
        "RSkinGloss.fx", "RSkinGlossColorize.fx", "RSkinHeat.fx",
        "RSkinOccludedUnit.fx", "RSkinShadowVolume.fx", "Skydome.fx",
        "TerrainMeshBump.fx", "TerrainMeshGloss.fx", "Tree.fx",
    };
    for (const char* s : kKnownShaders) {
        const std::string vfmt = default_vertex_format_for_shader(s);
        INFO("shader: " << s << " resolved to: '" << vfmt << "'");
        REQUIRE_FALSE(vfmt.empty());
        REQUIRE(is_recognized_vertex_format(vfmt));
    }
}

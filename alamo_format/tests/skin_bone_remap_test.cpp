// Catch2 tests for Phase 10.5 (issue #81): skin-bone-remap helper.

#include <catch2/catch_test_macros.hpp>

#include "alamo_format/skin_bone_remap.h"
#include "alamo_format/export_scene.h"

using alamo_format::apply_skin_bone_remap;
using alamo_format::vertex_format_needs_skin_remap;
using alamo_format::ExportSubmesh;
using alamo_format::ExportVertex;

namespace {

ExportVertex make_vertex(std::array<std::uint32_t, 4> bones,
                         std::array<float, 4>         weights) {
    ExportVertex v;
    v.bone_indices = bones;
    v.weights      = weights;
    return v;
}

}  // namespace

TEST_CASE("apply_skin_bone_remap: single bone across all vertices", "[skin_bone_remap]") {
    // Rigid-attachment case: every vertex points at bone 7, weights
    // (1, 0, 0, 0) in slot 0, unused slots default to bone 0.
    ExportSubmesh sub;
    for (int i = 0; i < 5; ++i) {
        sub.vertices.push_back(make_vertex({7u, 0u, 0u, 0u}, {1.f, 0.f, 0.f, 0.f}));
    }

    apply_skin_bone_remap(sub);

    // First-seen order: bone 7 in slot 0 first, then bone 0 in slots
    // 1..3. So remap = [7, 0].
    REQUIRE(sub.skin_bone_remap == std::vector<std::uint32_t>{7u, 0u});

    // Vertex bone_indices rewritten to local slots: 7 -> 0, 0 -> 1.
    for (const auto& v : sub.vertices) {
        REQUIRE(v.bone_indices[0] == 0u);  // local slot for global 7
        REQUIRE(v.bone_indices[1] == 1u);  // local slot for global 0
        REQUIRE(v.bone_indices[2] == 1u);
        REQUIRE(v.bone_indices[3] == 1u);
    }
}

TEST_CASE("apply_skin_bone_remap: multi-bone weighted skinning", "[skin_bone_remap]") {
    // Phase 5c-style: vertex influenced by multiple bones with varying
    // weights. Each vertex has up to 4 unique global bones in its slots.
    ExportSubmesh sub;
    sub.vertices.push_back(make_vertex({5u, 8u, 3u, 0u}, {0.5f, 0.3f, 0.2f, 0.0f}));
    sub.vertices.push_back(make_vertex({8u, 3u, 7u, 0u}, {0.6f, 0.3f, 0.1f, 0.0f}));

    apply_skin_bone_remap(sub);

    // First-seen scan: vertex[0] -> slots see 5, 8, 3, 0 -> remap [5,8,3,0]
    // vertex[1] -> slots see 8 (already), 3 (already), 7 (new), 0 (already)
    //           -> remap appends 7 -> [5, 8, 3, 0, 7]
    REQUIRE(sub.skin_bone_remap == std::vector<std::uint32_t>{5u, 8u, 3u, 0u, 7u});

    // vertex[0]: 5->0, 8->1, 3->2, 0->3
    REQUIRE(sub.vertices[0].bone_indices == std::array<std::uint32_t, 4>{0u, 1u, 2u, 3u});
    // vertex[1]: 8->1, 3->2, 7->4, 0->3
    REQUIRE(sub.vertices[1].bone_indices == std::array<std::uint32_t, 4>{1u, 2u, 4u, 3u});
}

TEST_CASE("apply_skin_bone_remap: empty submesh leaves remap empty", "[skin_bone_remap]") {
    ExportSubmesh sub;
    apply_skin_bone_remap(sub);
    REQUIRE(sub.skin_bone_remap.empty());
}

TEST_CASE("apply_skin_bone_remap: weights are preserved unchanged", "[skin_bone_remap]") {
    // Per-vertex weights aren't part of the remap -- only indices are
    // rewritten. The test pins that contract so a future refactor
    // doesn't accidentally touch the weight array.
    ExportSubmesh sub;
    sub.vertices.push_back(make_vertex({5u, 8u, 3u, 0u}, {0.5f, 0.3f, 0.15f, 0.05f}));

    apply_skin_bone_remap(sub);

    REQUIRE(sub.vertices[0].weights == std::array<float, 4>{0.5f, 0.3f, 0.15f, 0.05f});
}

TEST_CASE("apply_skin_bone_remap: idempotent on already-local data", "[skin_bone_remap]") {
    // Re-running on a submesh whose bone_indices are already small
    // local slots (e.g., re-emitting from a partially-mutated
    // ExportScene) doesn't blow up -- it just rebuilds a remap that
    // happens to be the identity 0..N-1.
    ExportSubmesh sub;
    sub.vertices.push_back(make_vertex({0u, 1u, 2u, 3u}, {0.25f, 0.25f, 0.25f, 0.25f}));

    apply_skin_bone_remap(sub);
    REQUIRE(sub.skin_bone_remap == std::vector<std::uint32_t>{0u, 1u, 2u, 3u});
    REQUIRE(sub.vertices[0].bone_indices == std::array<std::uint32_t, 4>{0u, 1u, 2u, 3u});

    // Second invocation: bone_indices already 0..3, remap rebuilt
    // identically.
    apply_skin_bone_remap(sub);
    REQUIRE(sub.skin_bone_remap == std::vector<std::uint32_t>{0u, 1u, 2u, 3u});
    REQUIRE(sub.vertices[0].bone_indices == std::array<std::uint32_t, 4>{0u, 1u, 2u, 3u});
}

TEST_CASE("apply_skin_bone_remap: deterministic ordering across runs", "[skin_bone_remap]") {
    // Same input, same vertex iteration order -> same remap. Pins
    // determinism (no unordered_map iteration leak).
    ExportSubmesh sub1;
    sub1.vertices.push_back(make_vertex({100u, 50u, 25u, 12u}, {0.5f, 0.3f, 0.15f, 0.05f}));
    sub1.vertices.push_back(make_vertex({50u, 25u, 100u, 12u}, {0.5f, 0.3f, 0.15f, 0.05f}));
    apply_skin_bone_remap(sub1);

    ExportSubmesh sub2;
    sub2.vertices.push_back(make_vertex({100u, 50u, 25u, 12u}, {0.5f, 0.3f, 0.15f, 0.05f}));
    sub2.vertices.push_back(make_vertex({50u, 25u, 100u, 12u}, {0.5f, 0.3f, 0.15f, 0.05f}));
    apply_skin_bone_remap(sub2);

    REQUIRE(sub1.skin_bone_remap == sub2.skin_bone_remap);
    REQUIRE(sub1.vertices[0].bone_indices == sub2.vertices[0].bone_indices);
    REQUIRE(sub1.vertices[1].bone_indices == sub2.vertices[1].bone_indices);
}

TEST_CASE("vertex_format_needs_skin_remap: skinned format families", "[skin_bone_remap]") {
    SECTION("RSkin* family") {
        REQUIRE(vertex_format_needs_skin_remap("alD3dVertRSkinNU2"));
        REQUIRE(vertex_format_needs_skin_remap("alD3dVertRSkinNU2C"));
        REQUIRE(vertex_format_needs_skin_remap("alD3dVertRSkinNU2U3U3"));
        REQUIRE(vertex_format_needs_skin_remap("alD3dVertRSkinNU2U3U3C"));
    }
    SECTION("B4I4* family") {
        REQUIRE(vertex_format_needs_skin_remap("alD3dVertB4I4NU2"));
        REQUIRE(vertex_format_needs_skin_remap("alD3dVertB4I4NU2C"));
        REQUIRE(vertex_format_needs_skin_remap("alD3dVertB4I4NU2U3U3"));
        REQUIRE(vertex_format_needs_skin_remap("alD3dVertB4I4NU2U3U3C"));
    }
    SECTION("case-insensitive") {
        REQUIRE(vertex_format_needs_skin_remap("ALD3DVERTRSKINNU2"));
        REQUIRE(vertex_format_needs_skin_remap("ald3dvertb4i4nu2"));
    }
}

TEST_CASE("vertex_format_needs_skin_remap: non-skinned formats", "[skin_bone_remap]") {
    REQUIRE_FALSE(vertex_format_needs_skin_remap("alD3dVertN"));
    REQUIRE_FALSE(vertex_format_needs_skin_remap("alD3dVertNU2"));
    REQUIRE_FALSE(vertex_format_needs_skin_remap("alD3dVertNU2C"));
    REQUIRE_FALSE(vertex_format_needs_skin_remap("alD3dVertNU2U3U3"));
    REQUIRE_FALSE(vertex_format_needs_skin_remap("alD3dVertNU2U3U3C"));
    REQUIRE_FALSE(vertex_format_needs_skin_remap("alD3dVertGrass"));
    REQUIRE_FALSE(vertex_format_needs_skin_remap("alD3dVertBillboard"));
    REQUIRE_FALSE(vertex_format_needs_skin_remap(""));
    REQUIRE_FALSE(vertex_format_needs_skin_remap("RSkin"));  // prefix without al-
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "alamo_format/skin_weights.h"

using Catch::Matchers::WithinAbs;
using alamo_format::skin::BoneWeight;
using alamo_format::skin::top4_normalized;

namespace {
constexpr float kTol = 1e-6f;
}

TEST_CASE("top4_normalized: empty input falls back to rigid binding") {
    auto b = top4_normalized({}, /*fallback=*/7u);
    REQUIRE(b.bone_indices == std::array<std::uint32_t, 4>{7u, 0u, 0u, 0u});
    REQUIRE(b.weights      == std::array<float, 4>{1.f, 0.f, 0.f, 0.f});
}

TEST_CASE("top4_normalized: all-zero weights fall back to rigid binding") {
    auto b = top4_normalized({{3u, 0.f}, {4u, 0.f}}, /*fallback=*/9u);
    REQUIRE(b.bone_indices[0] == 9u);
    REQUIRE(b.weights[0]      == 1.f);
    REQUIRE(b.bone_indices[1] == 0u);
    REQUIRE(b.weights[1]      == 0.f);
}

TEST_CASE("top4_normalized: single bone fills slot 0, others zero") {
    auto b = top4_normalized({{5u, 0.5f}}, /*fallback=*/0u);
    REQUIRE(b.bone_indices[0] == 5u);
    REQUIRE_THAT(b.weights[0], WithinAbs(1.f, kTol));    // renormalized
    for (int i = 1; i < 4; ++i) {
        REQUIRE(b.bone_indices[i] == 0u);
        REQUIRE(b.weights[i]      == 0.f);
    }
}

TEST_CASE("top4_normalized: three bones occupy slots 0..2, slot 3 zero, weights sum to 1") {
    auto b = top4_normalized({{1u, 0.5f}, {2u, 0.3f}, {3u, 0.2f}}, 0u);
    // Sort is descending by weight: 0.5, 0.3, 0.2
    REQUIRE(b.bone_indices[0] == 1u);
    REQUIRE(b.bone_indices[1] == 2u);
    REQUIRE(b.bone_indices[2] == 3u);
    REQUIRE(b.bone_indices[3] == 0u);
    REQUIRE_THAT(b.weights[0], WithinAbs(0.5f, kTol));
    REQUIRE_THAT(b.weights[1], WithinAbs(0.3f, kTol));
    REQUIRE_THAT(b.weights[2], WithinAbs(0.2f, kTol));
    REQUIRE(b.weights[3] == 0.f);
}

TEST_CASE("top4_normalized: four bones all occupy slots, sum = 1") {
    auto b = top4_normalized(
        {{10u, 0.4f}, {11u, 0.3f}, {12u, 0.2f}, {13u, 0.1f}}, 0u);
    REQUIRE(b.bone_indices[0] == 10u);
    REQUIRE(b.bone_indices[1] == 11u);
    REQUIRE(b.bone_indices[2] == 12u);
    REQUIRE(b.bone_indices[3] == 13u);
    float sum = b.weights[0] + b.weights[1] + b.weights[2] + b.weights[3];
    REQUIRE_THAT(sum, WithinAbs(1.f, kTol));
}

TEST_CASE("top4_normalized: drops smallest influence when >4 bones, renormalizes") {
    // Five bones, smallest is bone#15 with 0.05. Result should drop it
    // and renormalize the remaining four to sum to 1.0.
    auto b = top4_normalized(
        {{11u, 0.40f}, {12u, 0.25f}, {13u, 0.20f}, {14u, 0.10f}, {15u, 0.05f}},
        /*fallback=*/0u);

    // 0.05 dropped; remaining sum is 0.95. Normalized:
    //   0.40 / 0.95 ≈ 0.4210526
    //   0.25 / 0.95 ≈ 0.2631579
    //   0.20 / 0.95 ≈ 0.2105263
    //   0.10 / 0.95 ≈ 0.1052632
    REQUIRE(b.bone_indices[0] == 11u);
    REQUIRE(b.bone_indices[1] == 12u);
    REQUIRE(b.bone_indices[2] == 13u);
    REQUIRE(b.bone_indices[3] == 14u);

    REQUIRE_THAT(b.weights[0], WithinAbs(0.40f / 0.95f, kTol));
    REQUIRE_THAT(b.weights[1], WithinAbs(0.25f / 0.95f, kTol));
    REQUIRE_THAT(b.weights[2], WithinAbs(0.20f / 0.95f, kTol));
    REQUIRE_THAT(b.weights[3], WithinAbs(0.10f / 0.95f, kTol));
    float sum = b.weights[0] + b.weights[1] + b.weights[2] + b.weights[3];
    REQUIRE_THAT(sum, WithinAbs(1.f, kTol));
}

TEST_CASE("top4_normalized: ignores non-positive weights") {
    // Negative weight (would be a Max bug) should be treated as if absent.
    auto b = top4_normalized({{1u, 0.7f}, {2u, -0.3f}, {3u, 0.0f}}, 0u);
    REQUIRE(b.bone_indices[0] == 1u);
    REQUIRE_THAT(b.weights[0], WithinAbs(1.f, kTol));     // sole survivor → 1.0
    REQUIRE(b.bone_indices[1] == 0u);
    REQUIRE(b.weights[1]      == 0.f);
}

TEST_CASE("top4_normalized: 50/50 tie packs deterministically by bone_index") {
    // Equal weights -> tie-break on bone_index ascending so a smooth joint
    // always serializes the same way.
    auto a = top4_normalized({{20u, 0.5f}, {7u, 0.5f}}, 0u);
    auto b = top4_normalized({{7u, 0.5f}, {20u, 0.5f}}, 0u);  // input order flipped
    REQUIRE(a.bone_indices == b.bone_indices);
    REQUIRE(a.bone_indices[0] == 7u);
    REQUIRE(a.bone_indices[1] == 20u);
    REQUIRE_THAT(a.weights[0], WithinAbs(0.5f, kTol));
    REQUIRE_THAT(a.weights[1], WithinAbs(0.5f, kTol));
}

TEST_CASE("top4_normalized: unnormalized input is renormalized") {
    // Max sometimes hands out weights that already sum to 1 within float
    // precision, but Skin's "normalize weights" option can be off. Either
    // way, we always renormalize so the disk record always sums to 1.0.
    auto b = top4_normalized({{1u, 2.0f}, {2u, 2.0f}}, 0u);  // sums to 4
    REQUIRE_THAT(b.weights[0], WithinAbs(0.5f, kTol));
    REQUIRE_THAT(b.weights[1], WithinAbs(0.5f, kTol));
}

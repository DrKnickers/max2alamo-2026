#pragma once

// Per-vertex skin-binding selection (Phase 5c).
//
// The 144 B vertex record reserves exactly 4 boneIdx + 4 weight slots
// per vertex. Real Max Skin modifiers happily produce 5, 6, 10+
// influences per vertex; this module is the pure host-agnostic logic
// for collapsing an arbitrary input list down to top-4 + renormalize.
//
// Lives in alamo_format/ (not max2alamo/) so it's CI-testable without
// launching Max. The walker stays a thin adapter: it collects
// (bone_index, weight) pairs from IGameSkin and hands them off here.

#include <array>
#include <cstdint>
#include <vector>

namespace alamo_format::skin {

struct BoneWeight {
    std::uint32_t bone_index;
    float         weight;
};

// Per-vertex result, ready to copy into ExportVertex::bone_indices /
// ExportVertex::weights. Unused slots carry bone_index = 0 (Root
// sentinel — engine ignores them because slot weight is 0).
struct VertexBinding {
    std::array<std::uint32_t, 4> bone_indices{0u, 0u, 0u, 0u};
    std::array<float, 4>         weights     {1.f, 0.f, 0.f, 0.f};
};

// Pick the 4 largest-weight influences from `in`, normalize them to
// sum to 1.0, and pack into a VertexBinding. Non-positive weights are
// dropped (defensive — Max's Skin shouldn't produce them, but if a
// modifier did, they're meaningless deformation).
//
// If `in` is empty or every weight is <= 0, falls back to a rigid
// binding: { fallback_bone_index, 0, 0, 0 } with weights { 1, 0, 0, 0 }.
// Same sentinel a static-mesh vertex carries in Phase 5b — keeps the
// geometry from collapsing to Root if the source data is degenerate.
VertexBinding top4_normalized(const std::vector<BoneWeight>& in,
                              std::uint32_t fallback_bone_index);

}  // namespace alamo_format::skin

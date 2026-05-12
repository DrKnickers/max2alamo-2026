#include "alamo_format/skin_weights.h"

#include <algorithm>

namespace alamo_format::skin {

namespace {

VertexBinding rigid_fallback(std::uint32_t fallback_bone_index)
{
    VertexBinding out;
    out.bone_indices = {fallback_bone_index, 0u, 0u, 0u};
    out.weights      = {1.f, 0.f, 0.f, 0.f};
    return out;
}

}  // namespace

VertexBinding top4_normalized(const std::vector<BoneWeight>& in,
                              std::uint32_t fallback_bone_index)
{
    // Copy + drop non-positive weights. Stable enough — input sizes
    // here are per-vertex bone counts (~1-8), not worth optimising.
    std::vector<BoneWeight> filtered;
    filtered.reserve(in.size());
    for (const auto& bw : in) {
        if (bw.weight > 0.f) filtered.push_back(bw);
    }
    if (filtered.empty()) return rigid_fallback(fallback_bone_index);

    // Descending sort by weight, then keep first 4. Tie-break on
    // bone_index for determinism so two adjacent bones with identical
    // weight (the 50/50 joint case) always pack the same way.
    std::sort(filtered.begin(), filtered.end(),
              [](const BoneWeight& a, const BoneWeight& b) {
                  if (a.weight != b.weight) return a.weight > b.weight;
                  return a.bone_index < b.bone_index;
              });
    if (filtered.size() > 4) filtered.resize(4);

    float sum = 0.f;
    for (const auto& bw : filtered) sum += bw.weight;
    if (sum <= 0.f) return rigid_fallback(fallback_bone_index);

    VertexBinding out;
    out.bone_indices = {0u, 0u, 0u, 0u};
    out.weights      = {0.f, 0.f, 0.f, 0.f};
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        out.bone_indices[i] = filtered[i].bone_index;
        out.weights[i]      = filtered[i].weight / sum;
    }
    return out;
}

}  // namespace alamo_format::skin

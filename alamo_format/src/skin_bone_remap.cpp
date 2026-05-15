#include "alamo_format/skin_bone_remap.h"

#include <cctype>
#include <unordered_map>

namespace alamo_format {

namespace {

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(s[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

}  // namespace

void apply_skin_bone_remap(ExportSubmesh& submesh) {
    // First-seen order is deterministic given a deterministic vertex
    // iteration order, which the walker provides. unordered_map for
    // O(1) lookups; vector for the canonical ordered table.
    std::unordered_map<std::uint32_t, std::uint32_t> global_to_local;
    std::vector<std::uint32_t> local_to_global;
    local_to_global.reserve(8);  // typical submesh references <8 bones

    auto intern = [&](std::uint32_t g) -> std::uint32_t {
        const auto it = global_to_local.find(g);
        if (it != global_to_local.end()) return it->second;
        const auto local = static_cast<std::uint32_t>(local_to_global.size());
        global_to_local.emplace(g, local);
        local_to_global.push_back(g);
        return local;
    };

    // Two-pass to keep first-seen order stable across all 4 slots of
    // every vertex (interning has to happen before rewriting).
    for (auto& v : submesh.vertices) {
        for (auto& g : v.bone_indices) {
            g = intern(g);
        }
    }

    submesh.skin_bone_remap = std::move(local_to_global);
}

bool vertex_format_needs_skin_remap(std::string_view vertex_format_name) {
    // The skinned families are alD3dVertRSkin* (1-bone) and
    // alD3dVertB4I4* (4-bone) per AloViewer's VertexFormatNames table.
    // Match the prefix case-insensitively (AloViewer uses _stricmp).
    return starts_with_ci(vertex_format_name, "alD3dVertRSkin") ||
           starts_with_ci(vertex_format_name, "alD3dVertB4I4");
}

}  // namespace alamo_format

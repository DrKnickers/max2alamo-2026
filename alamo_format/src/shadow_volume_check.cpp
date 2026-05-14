#include "alamo_format/shadow_volume_check.h"

#include <map>
#include <utility>

namespace alamo_format {

namespace {

// Canonicalize an undirected edge so {a,b} and {b,a} map to the same key.
// We want LESS first, GREATER second so the std::map's tuple ordering is
// deterministic and a/b vs b/a never split the count across two entries.
std::pair<int, int> canonical_edge(int a, int b)
{
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
}

}  // namespace

ShadowVolumeReport check_shadow_volume_closed(
    const std::vector<PositionTriangle>& tris)
{
    std::map<std::pair<int, int>, int> edge_count;
    int considered = 0;

    for (const auto& t : tris) {
        // Skip degenerate triangles -- two coincident vertices yield a
        // zero-area triangle and an edge of length zero, which is
        // meaningless for the manifold check. Legacy authored content
        // sometimes contains these; PG's exporter tolerated them.
        if (t.v0 == t.v1 || t.v1 == t.v2 || t.v0 == t.v2) continue;
        ++considered;

        ++edge_count[canonical_edge(t.v0, t.v1)];
        ++edge_count[canonical_edge(t.v1, t.v2)];
        ++edge_count[canonical_edge(t.v2, t.v0)];
    }

    int non_manifold = 0;
    for (const auto& kv : edge_count) {
        if (kv.second != 2) ++non_manifold;
    }

    ShadowVolumeReport r;
    r.is_closed              = (non_manifold == 0);
    r.non_manifold_edge_count = non_manifold;
    r.triangle_count          = considered;
    return r;
}

}  // namespace alamo_format

#pragma once

// Phase 12: pure-C++ closed-volume validator for shadow-volume meshes.
//
// The Alamo engine renders stencil shadows from meshes whose material
// uses MeshShadowVolume.fx / RSkinShadowVolume.fx. The stencil algorithm
// requires the mesh to be a closed manifold -- every undirected edge
// shared by exactly two triangles -- otherwise the shadow has holes /
// "light leaks". The legacy Petroglyph max2alamo exporter warned (but
// did not abort) on open shadow meshes; Phase 12 matches that behavior.
//
// This validator is the format-library half of Phase 12. The walker-side
// half (in max2alamo/src/scene_walker.cpp) detects shadow-volume meshes,
// builds a position-deduplicated triangle list, calls this validator,
// and emits a warning to .export.log + the MAXScript Listener.
//
// Position-space dedup is the caller's responsibility: ALO export
// vertex-splits on UV / normal seams, which would falsely flag a closed
// mesh as open if we ran the check on raw vertex indices. The
// PositionTriangle type signature structurally enforces this -- callers
// pass already-deduped POSITION indices.

#include <cstddef>
#include <vector>

namespace alamo_format {

struct PositionTriangle {
    int v0;  // position-deduped index
    int v1;
    int v2;
};

struct ShadowVolumeReport {
    bool is_closed;               // true iff every edge belongs to exactly 2 triangles
    int  non_manifold_edge_count; // edges with incidence count != 2 (counts boundary AND >2-shared)
    int  triangle_count;          // non-degenerate triangles considered (zero-area excluded)
};

// Validate that the given position-deduplicated triangle list defines a
// closed 2-manifold (every edge shared by exactly 2 triangles).
//
// Zero-area triangles (any two of v0/v1/v2 equal in position-index space)
// are silently skipped -- legacy authored content sometimes contains
// degenerate triangles that should not poison the validity check.
//
// Empty input is "trivially closed" (is_closed=true, non_manifold=0).
// Callers that want to treat empty as a separate state should check
// triangle_count themselves.
//
// Cost: O(n log n) for n triangles (std::map of edge -> count). For
// typical mesh sizes (<= 50k tris) runs in well under 100ms; not worth
// optimizing past a std::map until profile evidence says so.
ShadowVolumeReport check_shadow_volume_closed(
    const std::vector<PositionTriangle>& tris);

}  // namespace alamo_format

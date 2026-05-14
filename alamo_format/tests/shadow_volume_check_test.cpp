// Catch2 tests for Phase 12's shadow-volume closed-manifold check.
//
// Each case is a single-fact assertion about the validator's behaviour
// on a hand-built triangle list. Tripwires K/L/M (in the plan) each
// correspond to one assertion that should fail if the validator is
// mutated in the named way.
//
// All triangles in this file use POSITION-DEDUPED indices -- the walker
// is responsible for the dedup; the validator only checks topology.

#include <catch2/catch_test_macros.hpp>

#include "alamo_format/shadow_volume_check.h"

#include <vector>

using alamo_format::PositionTriangle;
using alamo_format::ShadowVolumeReport;
using alamo_format::check_shadow_volume_closed;

namespace {

PositionTriangle T(int a, int b, int c) { return {a, b, c}; }

// Closed tetrahedron: 4 triangles, 6 edges, each edge in exactly 2 tris.
// Vertices 0..3, faces opposite each vertex (winding doesn't matter to
// the check, only topology).
std::vector<PositionTriangle> closed_tetrahedron()
{
    return { T(0,1,2), T(0,2,3), T(0,3,1), T(1,3,2) };
}

// Closed cube: 6 quad faces, each split into 2 triangles == 12 triangles.
// Vertex indices 0..7 of a unit cube:
//   0: (0,0,0)  1: (1,0,0)  2: (1,1,0)  3: (0,1,0)
//   4: (0,0,1)  5: (1,0,1)  6: (1,1,1)  7: (0,1,1)
// Face winding chosen so all faces share edges with their neighbours.
std::vector<PositionTriangle> closed_cube()
{
    return {
        T(0,1,2), T(0,2,3),  // bottom z=0
        T(4,6,5), T(4,7,6),  // top    z=1
        T(0,4,5), T(0,5,1),  // front  y=0
        T(2,6,7), T(2,7,3),  // back   y=1
        T(0,3,7), T(0,7,4),  // left   x=0
        T(1,5,6), T(1,6,2),  // right  x=1
    };
}

// Open cube: closed_cube() minus the top two triangles. The top face's
// four edges (4-5, 5-6, 6-7, 7-4) lose one incidence each, leaving them
// at count=1 (non-manifold boundary).
std::vector<PositionTriangle> open_cube()
{
    auto v = closed_cube();
    v.erase(v.begin() + 2, v.begin() + 4);  // drop top z=1 triangles
    return v;
}

}  // namespace

// =========================================================================
// Trivial / empty inputs
// =========================================================================

TEST_CASE("shadow_volume_check: empty input is trivially closed") {
    auto r = check_shadow_volume_closed({});
    REQUIRE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 0);
    REQUIRE(r.triangle_count == 0);
}

TEST_CASE("shadow_volume_check: single triangle has 3 boundary edges") {
    auto r = check_shadow_volume_closed({ T(0,1,2) });
    REQUIRE_FALSE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 3);
    REQUIRE(r.triangle_count == 1);
}

// =========================================================================
// Canonical closed shapes
// =========================================================================

TEST_CASE("shadow_volume_check: closed tetrahedron passes") {
    auto r = check_shadow_volume_closed(closed_tetrahedron());
    REQUIRE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 0);
    REQUIRE(r.triangle_count == 4);
}

TEST_CASE("shadow_volume_check: closed cube passes") {
    auto r = check_shadow_volume_closed(closed_cube());
    REQUIRE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 0);
    REQUIRE(r.triangle_count == 12);
}

TEST_CASE("shadow_volume_check: two disjoint closed tetrahedra both pass") {
    // Build a second tet on vertices 10..13 so the components share no
    // edges. Union should still be closed (each component is independently
    // closed; no edges cross between components).
    auto v = closed_tetrahedron();
    for (auto& t : { T(10,11,12), T(10,12,13), T(10,13,11), T(11,13,12) }) {
        v.push_back(t);
    }
    auto r = check_shadow_volume_closed(v);
    REQUIRE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 0);
    REQUIRE(r.triangle_count == 8);
}

// =========================================================================
// Open / non-manifold shapes
// =========================================================================

TEST_CASE("shadow_volume_check: open cube (missing top) has 4 boundary edges") {
    auto r = check_shadow_volume_closed(open_cube());
    REQUIRE_FALSE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 4);
    REQUIRE(r.triangle_count == 10);
}

TEST_CASE("shadow_volume_check: T-junction (3 tris share 1 edge) is non-manifold") {
    // Three triangles all sharing edge (0,1). The shared edge's incidence
    // count is 3, which is non-manifold (>2-sided is just as bad as <2).
    std::vector<PositionTriangle> tris = {
        T(0,1,2),
        T(0,1,3),
        T(0,1,4),
    };
    auto r = check_shadow_volume_closed(tris);
    REQUIRE_FALSE(r.is_closed);
    // The shared (0,1) edge has count=3. Each triangle's other two edges
    // (e.g. (0,2), (1,2) for triangle 0) are boundary edges with count=1.
    // 1 shared + 6 boundaries = 7 non-manifold edges total.
    REQUIRE(r.non_manifold_edge_count == 7);
    REQUIRE(r.triangle_count == 3);
}

// =========================================================================
// Degenerate triangle filtering
// =========================================================================

TEST_CASE("shadow_volume_check: zero-area triangle (v0==v1) is skipped") {
    auto v = closed_tetrahedron();
    v.push_back(T(5, 5, 6));  // degenerate: two coincident vertices
    auto r = check_shadow_volume_closed(v);
    REQUIRE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 0);
    REQUIRE(r.triangle_count == 4);  // degenerate skipped, not counted
}

TEST_CASE("shadow_volume_check: all-three-equal degenerate is skipped") {
    auto v = closed_tetrahedron();
    v.push_back(T(7, 7, 7));  // fully degenerate
    auto r = check_shadow_volume_closed(v);
    REQUIRE(r.is_closed);
    REQUIRE(r.triangle_count == 4);
}

// =========================================================================
// Winding (PG didn't check; neither do we) -- explicit pin via test name
// =========================================================================

TEST_CASE("shadow_volume_check: closed cube with one face wound reversed still passes") {
    // Topologically, edge incidence is independent of winding. PG's exporter
    // matched this behavior; we explicitly do too. (A consistent-winding
    // check is a separate, future concern.)
    auto v = closed_cube();
    // Reverse the bottom face's first triangle: T(0,1,2) -> T(0,2,1).
    v[0] = T(0,2,1);
    auto r = check_shadow_volume_closed(v);
    REQUIRE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 0);
}

// =========================================================================
// Tripwires -- each fails when the corresponding 1-line mutation is applied
// =========================================================================

TEST_CASE("shadow_volume_check: TRIPWIRE-K edges canonicalize {a,b}=={b,a}") {
    // Two triangles share an edge, but each lists the shared vertices
    // in opposite order: T(2,5,7) has edge (2,5); T(5,2,9) has edge (5,2).
    // The validator must collapse these to the same canonical key (2,5)
    // so the count reaches 2. If canonicalization is dropped, the count
    // splits 1/1 and the edge is reported as 2 non-manifold edges.
    std::vector<PositionTriangle> tris = {
        T(2,5,7), T(5,2,9),
        // Close off the rest of the topology so only this edge matters.
        T(7,2,9), T(7,9,5),
    };
    auto r = check_shadow_volume_closed(tris);
    REQUIRE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 0);
}

TEST_CASE("shadow_volume_check: TRIPWIRE-L single triangle must be flagged open") {
    // The most basic open mesh. If the check is "count < 1" instead of
    // "count != 2", this test reports closed=true (wrong).
    auto r = check_shadow_volume_closed({ T(0,1,2) });
    REQUIRE_FALSE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 3);
}

TEST_CASE("shadow_volume_check: TRIPWIRE-M degenerate triangles must not contribute edges") {
    // A single degenerate triangle alone: no real edges, so trivially
    // closed. If the filter is removed, the validator adds edges
    // (0,0), (0,1), (1,0) -- the self-loop and possibly bogus boundaries
    // -- and flags non-manifold.
    auto r = check_shadow_volume_closed({ T(0,0,1) });
    REQUIRE(r.is_closed);
    REQUIRE(r.non_manifold_edge_count == 0);
    REQUIRE(r.triangle_count == 0);
}

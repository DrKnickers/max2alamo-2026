// Catch2 tests for the Phase 9.2 collision-tree writer.
//
// Builds the 0x1200 subtree from synthetic triangle inputs, then parses
// the resulting bytes back per Petrolution's spec and validates the
// fields. Doesn't require the Max SDK; pure C++.

#include <catch2/catch_test_macros.hpp>

#include "alamo_format/collision_tree.h"
#include "alamo_format/chunk_tree.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using alamo_format::CollisionTriangle;
using alamo_format::ChunkNode;
using alamo_format::build_collision_tree;

namespace {

CollisionTriangle make_tri(std::uint16_t face,
                           std::array<float, 3> mn,
                           std::array<float, 3> mx)
{
    CollisionTriangle t;
    t.face_index = face;
    t.aabb_min   = mn;
    t.aabb_max   = mx;
    for (int a = 0; a < 3; ++a) t.centroid[a] = (mn[a] + mx[a]) * 0.5f;
    return t;
}

// Parse a uint32 LE from bytes at `off`.
std::uint32_t r_u32(const std::vector<std::uint8_t>& v, std::size_t off) {
    return std::uint32_t(v[off]) |
           std::uint32_t(v[off + 1]) << 8 |
           std::uint32_t(v[off + 2]) << 16 |
           std::uint32_t(v[off + 3]) << 24;
}

// Parse a uint16 LE.
std::uint16_t r_u16(const std::vector<std::uint8_t>& v, std::size_t off) {
    return std::uint16_t(v[off] | (v[off + 1] << 8));
}

// Parse a float32 LE.
float r_f32(const std::vector<std::uint8_t>& v, std::size_t off) {
    std::uint32_t bits = r_u32(v, off);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Find a chunk by id at the top of a container.
const ChunkNode* find_child(const ChunkNode& c, std::uint32_t id) {
    for (const auto& k : c.children) if (k.id == id) return &k;
    return nullptr;
}

}  // namespace

// =========================================================================
// Empty input
// =========================================================================

TEST_CASE("collision_tree: empty triangle list produces well-formed empty tree") {
    auto root = build_collision_tree({}, {0,0,0}, {0,0,0});
    REQUIRE(root.id == 0x1200);
    REQUIRE(root.is_container);
    REQUIRE(root.children.size() == 3);

    const auto* tree_info = find_child(root, 0x1201);
    REQUIRE(tree_info);
    REQUIRE(tree_info->payload.size() == 40);   // four mini-chunks per spec

    const auto* nodes = find_child(root, 0x1202);
    REQUIRE(nodes);
    REQUIRE(nodes->payload.size() == 10);       // one (empty) leaf record

    const auto* prims = find_child(root, 0x1203);
    REQUIRE(prims);
    REQUIRE(prims->payload.empty());            // no primitives
}

// =========================================================================
// 0x1201 mini-chunk layout
// =========================================================================

TEST_CASE("collision_tree: 0x1201 mini-chunks match Petrolution spec") {
    std::vector<CollisionTriangle> tris = {
        make_tri(0, {0,0,0}, {1,1,1}),
    };
    auto root = build_collision_tree(tris, {-5,-5,-5}, {5,5,5});
    const auto* ti = find_child(root, 0x1201);
    REQUIRE(ti);
    REQUIRE(ti->payload.size() == 40);

    // Mini-chunk 00: bbox_min (float3)
    REQUIRE(ti->payload[0] == 0x00);
    REQUIRE(ti->payload[1] == 12);
    REQUIRE(r_f32(ti->payload, 2) == -5.f);
    REQUIRE(r_f32(ti->payload, 6) == -5.f);
    REQUIRE(r_f32(ti->payload, 10) == -5.f);

    // Mini-chunk 01: bbox_max (float3)
    REQUIRE(ti->payload[14] == 0x01);
    REQUIRE(ti->payload[15] == 12);
    REQUIRE(r_f32(ti->payload, 16) == 5.f);
    REQUIRE(r_f32(ti->payload, 20) == 5.f);
    REQUIRE(r_f32(ti->payload, 24) == 5.f);

    // Mini-chunk 02: nNodes (dword)
    REQUIRE(ti->payload[28] == 0x02);
    REQUIRE(ti->payload[29] == 4);
    const std::uint32_t n_nodes = r_u32(ti->payload, 30);
    REQUIRE(n_nodes >= 1);  // at least the root/leaf

    // Mini-chunk 03: nPrimitives (dword)
    REQUIRE(ti->payload[34] == 0x03);
    REQUIRE(ti->payload[35] == 4);
    const std::uint32_t n_prims = r_u32(ti->payload, 36);
    REQUIRE(n_prims == 1);  // one triangle in, one in 0x1203
}

// =========================================================================
// 0x1202 record layout + 0x1203 face indices
// =========================================================================

TEST_CASE("collision_tree: single triangle => single leaf record + 1 primitive") {
    auto root = build_collision_tree({ make_tri(7, {0,0,0}, {1,1,1}) },
                                     {0,0,0}, {1,1,1});

    const auto* nodes = find_child(root, 0x1202);
    REQUIRE(nodes);
    REQUIRE(nodes->payload.size() == 10);  // one 10-byte record

    // Record fields: byte3 min (0..0), byte3 max (255..255), nPrim=1, link=0
    for (int i = 0; i < 3; ++i) REQUIRE(nodes->payload[i]     == 0);
    for (int i = 3; i < 6; ++i) REQUIRE(nodes->payload[i]     == 255);
    REQUIRE(r_u16(nodes->payload, 6) == 1);   // nPrimitives
    REQUIRE(r_u16(nodes->payload, 8) == 0);   // link = start index in 0x1203

    const auto* prims = find_child(root, 0x1203);
    REQUIRE(prims);
    REQUIRE(prims->payload.size() == 2);     // one uint16
    REQUIRE(r_u16(prims->payload, 0) == 7);  // matches face_index in the input
}

TEST_CASE("collision_tree: 12 triangles split into multiple leaves") {
    // 12 triangles in a 4x3 grid; centroids spread along X.
    std::vector<CollisionTriangle> tris;
    for (int i = 0; i < 12; ++i) {
        tris.push_back(make_tri(
            static_cast<std::uint16_t>(i),
            {float(i),       0.f, 0.f},
            {float(i) + 1.f, 1.f, 1.f}));
    }
    auto root = build_collision_tree(tris, {0,0,0}, {12,1,1});

    const auto* ti = find_child(root, 0x1201);
    REQUIRE(ti);
    const std::uint32_t n_nodes = r_u32(ti->payload, 30);
    const std::uint32_t n_prims = r_u32(ti->payload, 36);
    REQUIRE(n_prims == 12);                // every triangle gets a slot
    REQUIRE(n_nodes >= 3);                 // at least root + 2 leaves
    REQUIRE(n_nodes <= 24);                // sanity upper bound

    const auto* nodes = find_child(root, 0x1202);
    REQUIRE(nodes);
    REQUIRE(nodes->payload.size() == n_nodes * 10);  // 10 bytes per record

    const auto* prims = find_child(root, 0x1203);
    REQUIRE(prims);
    REQUIRE(prims->payload.size() == 24);  // 12 uint16s

    // Every face index 0..11 appears exactly once across 0x1203
    // (median-split preserves the set; only the order changes).
    std::vector<bool> seen(12, false);
    for (std::size_t off = 0; off < prims->payload.size(); off += 2) {
        const auto idx = r_u16(prims->payload, off);
        REQUIRE(idx < 12);
        REQUIRE_FALSE(seen[idx]);
        seen[idx] = true;
    }
}

// =========================================================================
// Tripwires
// =========================================================================

TEST_CASE("collision_tree: TRIPWIRE-N face indices preserved (no loss / no dup)") {
    // If a builder bug drops or duplicates a triangle, 0x1203's count
    // or set will diverge from the input.
    std::vector<CollisionTriangle> tris;
    for (int i = 0; i < 7; ++i) {
        tris.push_back(make_tri(
            static_cast<std::uint16_t>(100 + i),    // distinctive face indices
            {float(i),       0.f, 0.f},
            {float(i) + 1.f, 1.f, 1.f}));
    }
    auto root = build_collision_tree(tris, {0,0,0}, {7,1,1});
    const auto* prims = find_child(root, 0x1203);
    REQUIRE(prims);
    REQUIRE(prims->payload.size() == 14);  // 7 uint16s

    // The mapping array must contain {100, 101, 102, 103, 104, 105, 106}
    // in some order.
    std::vector<std::uint16_t> got;
    for (std::size_t off = 0; off < prims->payload.size(); off += 2) {
        got.push_back(r_u16(prims->payload, off));
    }
    std::sort(got.begin(), got.end());
    for (int i = 0; i < 7; ++i) REQUIRE(got[i] == 100 + i);
}

TEST_CASE("collision_tree: TRIPWIRE-O nNodes header matches 0x1202 record count") {
    // If a builder bug miscounts nodes, the header in 0x1201 will not
    // match the actual record count in 0x1202 / 10 bytes.
    std::vector<CollisionTriangle> tris;
    for (int i = 0; i < 20; ++i) {
        tris.push_back(make_tri(
            static_cast<std::uint16_t>(i),
            {float(i),       0.f, 0.f},
            {float(i) + 1.f, 1.f, 1.f}));
    }
    auto root = build_collision_tree(tris, {0,0,0}, {20,1,1});

    const auto* ti = find_child(root, 0x1201);
    const auto* nodes = find_child(root, 0x1202);
    REQUIRE(ti);
    REQUIRE(nodes);
    const std::uint32_t header_nNodes = r_u32(ti->payload, 30);
    REQUIRE(nodes->payload.size() == header_nNodes * 10);
}

TEST_CASE("collision_tree: TRIPWIRE-P byte3-quantization is monotonic") {
    // If quantization is broken (e.g. off-by-one or wrong axis), a
    // triangle authored at the parent-bbox max corner should map to 255,
    // not 0 or some middle value.
    std::vector<CollisionTriangle> tris = {
        make_tri(0, {10.f, 20.f, 30.f}, {10.f, 20.f, 30.f}),   // single point at max corner
    };
    auto root = build_collision_tree(tris, {0,0,0}, {10,20,30});
    const auto* nodes = find_child(root, 0x1202);
    REQUIRE(nodes);
    REQUIRE(nodes->payload.size() >= 10);
    // Leaf's byte3 min and max both = (255,255,255) since the triangle's
    // AABB is a single point at the parent-bbox max corner.
    for (int i = 0; i < 6; ++i) REQUIRE(nodes->payload[i] == 255);
}

TEST_CASE("collision_tree: degenerate parent-bbox extent => byte3 fields are 0") {
    // If the parent bbox is a single point (extent zero on every axis),
    // quantization should not divide by zero. All byte3 fields become 0.
    std::vector<CollisionTriangle> tris = {
        make_tri(0, {5,5,5}, {5,5,5}),
    };
    auto root = build_collision_tree(tris, {5,5,5}, {5,5,5});
    const auto* nodes = find_child(root, 0x1202);
    REQUIRE(nodes);
    REQUIRE(nodes->payload.size() >= 10);
    for (int i = 0; i < 6; ++i) REQUIRE(nodes->payload[i] == 0);
}

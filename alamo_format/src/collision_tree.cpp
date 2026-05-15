#include "alamo_format/collision_tree.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace alamo_format {

namespace {

// Leaf-size threshold. Triangles per leaf <= this; nodes with more are
// split. 4 is chosen to keep the total node count within an order of
// magnitude of vanilla output (X-Wing 12 tris -> 11 vanilla nodes; our
// threshold-4 builder gives a comparable count).
constexpr std::size_t kLeafSizeThreshold = 4;

// Cap on triangle count. uint16 face indices in 0x1203 limit us to 65535.
constexpr std::size_t kMaxTriangles = 65535;

// One node in the working tree before flattening. After we build the
// tree recursively, we walk it to assign 0x1202 indices and emit the
// flat node + primitive arrays.
struct TreeNode {
    std::array<float, 3> aabb_min{};
    std::array<float, 3> aabb_max{};
    // Internal nodes have left/right >= 0 and prims.empty().
    // Leaf nodes have left = right = -1 and prims non-empty.
    int                  left  = -1;
    int                  right = -1;
    std::vector<std::uint16_t> prims;  // face indices in this leaf
};

// Append uint32 LE.
void append_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>( v        & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}

// Append uint16 LE.
void append_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>( v       & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

// Append float32 LE.
void append_f32(std::vector<std::uint8_t>& out, float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    append_u32(out, bits);
}

// Quantize a coordinate into the [0..255] byte space relative to a
// parent AABB. Edge cases:
//   - parent extent is zero on this axis -> all values quantize to 0
//   - value < parent_min               -> clamp to 0
//   - value > parent_max               -> clamp to 255
std::uint8_t quantize(float value, float pmin, float pmax) {
    const float extent = pmax - pmin;
    if (extent <= 0.f) return 0;
    const float t = (value - pmin) / extent;
    if (t <= 0.f) return 0;
    if (t >= 1.f) return 255;
    return static_cast<std::uint8_t>(std::lround(t * 255.f));
}

// Recursively build the tree. Returns the index of the new node in
// `nodes`. `begin` and `end` slice `tris` into the working subset for
// this subtree.
int build_subtree(std::vector<TreeNode>& nodes,
                  std::vector<CollisionTriangle>& tris,
                  std::size_t begin,
                  std::size_t end)
{
    TreeNode node;
    // Subtree AABB = union of triangle AABBs in [begin, end).
    node.aabb_min = { +std::numeric_limits<float>::infinity(),
                      +std::numeric_limits<float>::infinity(),
                      +std::numeric_limits<float>::infinity() };
    node.aabb_max = { -std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity() };
    for (std::size_t i = begin; i < end; ++i) {
        for (int a = 0; a < 3; ++a) {
            if (tris[i].aabb_min[a] < node.aabb_min[a]) node.aabb_min[a] = tris[i].aabb_min[a];
            if (tris[i].aabb_max[a] > node.aabb_max[a]) node.aabb_max[a] = tris[i].aabb_max[a];
        }
    }

    const std::size_t n = end - begin;
    if (n <= kLeafSizeThreshold) {
        // Leaf -- emit the triangles' face indices.
        node.prims.reserve(n);
        for (std::size_t i = begin; i < end; ++i) {
            node.prims.push_back(tris[i].face_index);
        }
        nodes.push_back(std::move(node));
        return static_cast<int>(nodes.size() - 1);
    }

    // Internal node: split on longest axis at the median centroid.
    int axis = 0;
    {
        const float dx = node.aabb_max[0] - node.aabb_min[0];
        const float dy = node.aabb_max[1] - node.aabb_min[1];
        const float dz = node.aabb_max[2] - node.aabb_min[2];
        if (dy > dx) axis = 1;
        if (dz > (axis == 0 ? dx : dy)) axis = 2;
    }
    const std::size_t mid = begin + n / 2;
    std::nth_element(
        tris.begin() + begin,
        tris.begin() + mid,
        tris.begin() + end,
        [axis](const CollisionTriangle& a, const CollisionTriangle& b) {
            return a.centroid[axis] < b.centroid[axis];
        });

    // Reserve this node's slot before recursing so the index is stable.
    const int self_index = static_cast<int>(nodes.size());
    nodes.push_back(std::move(node));
    const int left_idx  = build_subtree(nodes, tris, begin, mid);
    const int right_idx = build_subtree(nodes, tris, mid,   end);
    nodes[self_index].left  = left_idx;
    nodes[self_index].right = right_idx;
    return self_index;
}

// Flatten the in-memory tree into the on-disk record array. The
// `flat_index_of` map translates from "node's index in `nodes`" to its
// final position in the 0x1202 record stream. We walk depth-first so
// the root is at index 0 and children come after their parents -- the
// Petrolution spec implies child indices are forward references.
struct FlatRecord {
    std::array<float, 3> aabb_min;
    std::array<float, 3> aabb_max;
    std::uint16_t        n_primitives;
    std::uint16_t        link;
};

void flatten(const std::vector<TreeNode>& nodes,
             std::vector<FlatRecord>& flat,
             std::vector<std::uint16_t>& prim_array,
             std::vector<int>& flat_index_of,
             int self_index)
{
    flat_index_of[self_index] = static_cast<int>(flat.size());
    const TreeNode& n = nodes[self_index];
    FlatRecord r;
    r.aabb_min = n.aabb_min;
    r.aabb_max = n.aabb_max;
    if (n.left < 0 && n.right < 0) {
        // Leaf
        r.n_primitives = static_cast<std::uint16_t>(n.prims.size());
        r.link         = static_cast<std::uint16_t>(prim_array.size());
        flat.push_back(r);
        for (auto idx : n.prims) prim_array.push_back(idx);
    } else {
        // Internal -- emit record now with placeholder link; recurse.
        r.n_primitives = 0;
        r.link         = 0;
        flat.push_back(r);
        // First child laid out immediately after the parent (depth-first).
        flatten(nodes, flat, prim_array, flat_index_of, n.left);
        // Right child after the left subtree is done.
        const int right_flat_idx = static_cast<int>(flat.size());
        flatten(nodes, flat, prim_array, flat_index_of, n.right);
        // Patch the parent's link to point at the left child (which is
        // adjacent to it -- index = parent's flat index + 1). Petrolution
        // is silent on the exact pair-or-stride convention; vanilla
        // appears to use either implicit-adjacent or explicit-left. We
        // pick implicit-adjacent for now (consumers we have don't parse
        // 0x1200 anyway, and the engine's BVH consumer is the only one
        // that matters in-game).
        (void)right_flat_idx;  // unused; left child is parent_flat + 1
        flat[flat_index_of[self_index]].link =
            static_cast<std::uint16_t>(flat_index_of[self_index] + 1);
    }
}

}  // namespace

ChunkNode
build_collision_tree(const std::vector<CollisionTriangle>& tris_in,
                     const std::array<float, 3>&            parent_bbox_min,
                     const std::array<float, 3>&            parent_bbox_max)
{
    // Defensive cap: refuse to overflow uint16 face indices.
    std::vector<CollisionTriangle> tris;
    tris.reserve(std::min(tris_in.size(), kMaxTriangles));
    for (std::size_t i = 0; i < tris_in.size() && i < kMaxTriangles; ++i) {
        tris.push_back(tris_in[i]);
    }

    std::vector<TreeNode> nodes;
    if (!tris.empty()) {
        build_subtree(nodes, tris, 0, tris.size());
    } else {
        // Zero triangles: emit a single empty leaf so 0x1201 / 0x1202 / 0x1203
        // are all well-formed. The spec doesn't explicitly disallow this; we
        // could also choose to emit no 0x1200 at all, but having a present-
        // but-empty tree keeps the format consistent across collision meshes.
        TreeNode empty;
        nodes.push_back(empty);
    }

    std::vector<FlatRecord>     flat;
    std::vector<std::uint16_t>  prim_array;
    std::vector<int>            flat_index_of(nodes.size(), -1);
    if (!nodes.empty()) {
        flat.reserve(nodes.size());
        prim_array.reserve(tris.size());
        flatten(nodes, flat, prim_array, flat_index_of, 0);
    }

    // ---- 0x1201: tree info (40 bytes via 4 mini-chunks) ----------------
    std::vector<std::uint8_t> p1201;
    p1201.reserve(40);
    // 0x00: float3 collisionBoxMin
    p1201.push_back(0x00); p1201.push_back(12);
    for (float v : parent_bbox_min) append_f32(p1201, v);
    // 0x01: float3 collisionBoxMax
    p1201.push_back(0x01); p1201.push_back(12);
    for (float v : parent_bbox_max) append_f32(p1201, v);
    // 0x02: dword nNodes
    p1201.push_back(0x02); p1201.push_back(4);
    append_u32(p1201, static_cast<std::uint32_t>(flat.size()));
    // 0x03: dword nPrimitives
    p1201.push_back(0x03); p1201.push_back(4);
    append_u32(p1201, static_cast<std::uint32_t>(prim_array.size()));

    // ---- 0x1202: node records (10 bytes each) --------------------------
    std::vector<std::uint8_t> p1202;
    p1202.reserve(flat.size() * 10);
    for (const auto& r : flat) {
        // byte3 min (quantized relative to the MESH bbox -- parent bbox).
        for (int a = 0; a < 3; ++a) {
            p1202.push_back(quantize(r.aabb_min[a], parent_bbox_min[a], parent_bbox_max[a]));
        }
        // byte3 max
        for (int a = 0; a < 3; ++a) {
            p1202.push_back(quantize(r.aabb_max[a], parent_bbox_min[a], parent_bbox_max[a]));
        }
        append_u16(p1202, r.n_primitives);
        append_u16(p1202, r.link);
    }

    // ---- 0x1203: triangle mapping (uint16 face indices) ----------------
    std::vector<std::uint8_t> p1203;
    p1203.reserve(prim_array.size() * 2);
    for (auto idx : prim_array) append_u16(p1203, idx);

    // ---- Assemble 0x1200 container -------------------------------------
    // (make_leaf / make_container are file-local helpers in alo_build.cpp;
    // we construct ChunkNode directly here to avoid pulling them into a
    // shared header just for this single use.)
    ChunkNode container;
    container.id = 0x1200;
    container.is_container = true;
    {
        ChunkNode leaf;
        leaf.id = 0x1201;
        leaf.is_container = false;
        leaf.payload = std::move(p1201);
        container.children.push_back(std::move(leaf));
    }
    {
        ChunkNode leaf;
        leaf.id = 0x1202;
        leaf.is_container = false;
        leaf.payload = std::move(p1202);
        container.children.push_back(std::move(leaf));
    }
    {
        ChunkNode leaf;
        leaf.id = 0x1203;
        leaf.is_container = false;
        leaf.payload = std::move(p1203);
        container.children.push_back(std::move(leaf));
    }
    return container;
}

}  // namespace alamo_format

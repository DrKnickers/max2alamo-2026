#pragma once

// Phase 9.2 -- collision-tree writer for the 0x1200 chunk family.
//
// Per Petrolution's spec (modtools.petrolution.net/docs/AloFileFormat):
//   0x1200  container          collision tree
//     0x1201  mini-chunks (40 bytes total):
//         00 float3  collisionBoxMin
//         01 float3  collisionBoxMax
//         02 dword   nNodes
//         03 dword   nPrimitives
//     0x1202  array of 10-byte AABB-tree nodes:
//         byte3   min     (8-bit-quantized relative to parent box)
//         byte3   max     (likewise)
//         word    nPrimitives    (0 = internal node, >0 = leaf)
//         word    link           (when nPrimitives == 0: first-child index
//                                 in 0x1202; when nPrimitives > 0: start
//                                 index in 0x1203)
//     0x1203  uint16[nPrimitives]  face indices into the mesh's 0x10004
//
// We build a simple median-axis-split AABB tree with a leaf-size threshold
// of 4 triangles (matches the order of magnitude of vanilla node counts:
// the X-Wing's 12-triangle collisionbox produces 11 vanilla nodes, vs.
// our threshold-4 builder which gives a similar count).
//
// The engine has a load-time runtime fallback (BVH rebuild) for `.alo`
// files lacking a 0x1200 chunk -- pre-9.2 modder-shipped exports collide
// in-game without one. Writing the tree at export time is purely an
// optimization + a step toward byte-shape parity with vanilla content.

#include "alamo_format/chunk_tree.h"

#include <array>
#include <cstdint>
#include <vector>

namespace alamo_format {

// One triangle to feed into the tree builder. The caller (alo_build) pulls
// these from the mesh's 0x10005/0x10007 vertex data + 0x10004 face data:
//   face_index = the i-th triangle's slot in the 0x1203 mapping
//   aabb       = the triangle's own bounding box in mesh-local space
//   centroid   = the median-split classifier
struct CollisionTriangle {
    std::uint16_t        face_index;
    std::array<float, 3> aabb_min;
    std::array<float, 3> aabb_max;
    std::array<float, 3> centroid;
};

// Build the 0x1200 subtree. `parent_bbox_min` / `parent_bbox_max` are the
// MESH's overall AABB (used as the reference frame for byte3 node-bbox
// quantization). Returns a ChunkNode for 0x1200 with three children
// (0x1201, 0x1202, 0x1203). Caller appends this to the mesh's 0x10000
// geometry block.
//
// Constraints: face_index must fit in uint16 (per the spec); we assert
// this via the caller. tris.size() <= 65535. Empty input produces an
// empty 0x1200 (one leaf with zero primitives, valid per the spec).
ChunkNode
build_collision_tree(const std::vector<CollisionTriangle>& tris,
                     const std::array<float, 3>&            parent_bbox_min,
                     const std::array<float, 3>&            parent_bbox_max);

}  // namespace alamo_format

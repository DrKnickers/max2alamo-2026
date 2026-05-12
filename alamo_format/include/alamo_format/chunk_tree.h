#pragma once

// Structural representation of an .alo / .ala file's chunk tree. Containers
// hold child chunks; leaves hold raw payload bytes. This is intentionally
// non-semantic — it preserves every byte of every chunk verbatim, which is
// what the round-trip oracle needs to validate the writer against the reader.
//
// Phase 4+ will add a higher-level AloModel layer on top of this that
// interprets specific chunks (bones, meshes, materials, etc.) — but
// round-trip correctness should not require semantic understanding.

#include "alamo_format/chunk_io.h"

#include <cstdint>
#include <vector>

namespace alamo_format {

struct ChunkNode {
    std::uint32_t id = 0;
    bool is_container = false;
    // Exactly one is populated, determined by `is_container`.
    std::vector<ChunkNode> children;
    std::vector<std::uint8_t> payload;

    bool is_leaf() const noexcept { return !is_container; }
};

// Parse a complete chunk-framed buffer into a tree. Top-level returns the
// sequence of chunks at file scope (.alo has no enclosing root chunk).
std::vector<ChunkNode> read_chunk_tree(const std::uint8_t* data, std::size_t size);

// Serialize a tree back to bytes. The output is byte-identical to the
// original input if the tree was produced by read_chunk_tree() (provided
// the source file was itself well-formed).
std::vector<std::uint8_t> write_chunk_tree(const std::vector<ChunkNode>& nodes);

}  // namespace alamo_format

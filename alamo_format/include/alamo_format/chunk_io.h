#pragma once

#include <cstdint>
#include <vector>

namespace alamo_format {

// Petroglyph's chunked-file framing.
//
// Layout: each chunk is `[uint32 id][uint32 size_with_flag][payload]`, where
//   size_with_flag = (payload_size & 0x7FFFFFFF) | (is_container ? 0 : 0x80000000)
// The high bit indicates a leaf (data) chunk vs. a container chunk holding
// further chunks. Sizes are little-endian.
//
// Reference: https://modtools.petrolution.net/docs/AloFileFormat
//   and Blender-ALAMO-Plugin/io_alamo_tools/export_alo.py (chunk_size helper).

constexpr std::uint32_t kSizeMask = 0x7FFFFFFFu;
constexpr std::uint32_t kLeafFlag = 0x80000000u;

inline std::uint32_t encode_chunk_size(std::uint32_t payload_size, bool is_leaf) noexcept {
    return (payload_size & kSizeMask) | (is_leaf ? kLeafFlag : 0u);
}

inline std::uint32_t decode_payload_size(std::uint32_t encoded) noexcept {
    return encoded & kSizeMask;
}

inline bool is_leaf_chunk(std::uint32_t encoded) noexcept {
    return (encoded & kLeafFlag) != 0u;
}

// Chunk writer / reader implementations land in chunk_writer.cpp / chunk_reader.cpp
// during Phase 1. This header just establishes the framing constants so that
// downstream headers (alo_model.h, etc.) can reference them.

}  // namespace alamo_format

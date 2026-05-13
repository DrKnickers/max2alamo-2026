#pragma once

// Typed in-memory representation of a .ala animation file.
//
// Reference: docs/format-notes.md:386-505 (chunk layout, FoC vs EaW, quat /
// position / scale / visibility encodings) and Mike Lankamp's alamo2max.ms
// :749-931 (reader cross-reference).
//
// Design note: AlaAnimation carries both typed fields (so the Phase 8b+
// walker can populate them directly) and raw byte payloads at every leaf
// (so the Phase 8a typed read+write pair preserves byte-identity on the
// vanilla FoC corpus). When build_ala() sees a non-empty raw payload, it
// emits the raw bytes verbatim; when it's empty, it synthesises canonical
// bytes from the typed fields. read_ala() always populates raw payloads
// AND parses typed fields from them, so a corpus -> AlaAnimation -> bytes
// round-trip is identity-preserving by construction.

#include "alamo_format/chunk_tree.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace alamo_format {

struct AlaBoneTrack {
    // ---- Typed view (mini-chunks inside 0x1003) ---------------------------
    std::string  name;                                       // mini 4
    std::uint32_t skeleton_index = 0;                        // mini 5

    float trans_offset[3] = {0, 0, 0};                       // mini 6
    float trans_scale[3]  = {0, 0, 0};                       // mini 7
    float scale_offset[3] = {1, 1, 1};                       // mini 8
    float scale_scale[3]  = {0, 0, 0};                       // mini 9

    // FoC-only indices into file-scope pools. -1 = no track of that kind.
    std::int16_t idx_translation = -1;                       // mini 14
    std::int16_t idx_scale       = -1;                       // mini 15
    std::int16_t idx_rotation    = -1;                       // mini 16

    // FoC-only default rotation (when idx_rotation < 0). int16 XYZW.
    std::array<std::int16_t, 4> default_rotation = {0, 0, 0, 32767};  // mini 17

    // ---- Round-trip preservation -----------------------------------------
    // Populated by read_ala() with the original 0x1003 leaf payload (the
    // mini-chunks in their original order). build_ala() emits this verbatim
    // when non-empty; otherwise it synthesises a canonical 0x1003 from the
    // typed fields above. Phase 8a always uses the raw path (byte-identity).
    std::vector<std::uint8_t> raw_info_payload;

    // Inner leaves of this 0x1002 other than 0x1003 — preserved in original
    // order. Holds 0x1004 / 0x1005 / 0x1006 (EaW per-bone tracks), 0x1007
    // (visibility, both formats), 0x1008 (rare unknown leaf in vanilla).
    // build_ala() emits these verbatim after the 0x1003.
    std::vector<ChunkNode> track_leaves;
};

struct AlaAnimation {
    // ---- Typed view (mini-chunks inside 0x1001) ---------------------------
    std::uint32_t n_frames = 0;                              // mini 1
    float         fps      = 30.0f;                          // mini 2
    // n_bones is derived from bones.size(); not stored separately.

    // FoC-only pool dimensions. Zero = pool not present.
    std::uint32_t n_rotation_words    = 0;                   // mini 11
    std::uint32_t n_translation_words = 0;                   // mini 12
    std::uint32_t n_scale_words       = 0;                   // mini 13

    std::vector<AlaBoneTrack> bones;

    // FoC-only file-scope track pools (flat int16 streams). Empty = EaW
    // file (or FoC file with that pool absent because its word-count is 0).
    // On disk: 0x100a (translation) appears before 0x1009 (rotation) — see
    // docs/format-notes.md and corpus observation
    // (e.g. EI_DARKTROOPER_ONE_WALKMOVE_00.ALA: 0x100a at offset 2870, then
    // 0x1009 at offset 3568).
    std::vector<std::int16_t> rotation_pool;     // 0x1009
    std::vector<std::int16_t> translation_pool;  // 0x100a

    // ---- Round-trip preservation -----------------------------------------
    // Populated by read_ala() with the original 0x1001 leaf payload (mini-
    // chunks in their original order). build_ala() emits verbatim when
    // non-empty; otherwise synthesises canonical 0x1001 from typed fields.
    std::vector<std::uint8_t> raw_info_payload;

    // True iff this animation uses the FoC track-pool format. The detection
    // signal (per docs/format-notes.md:498 and alamo2max.ms:855-861) is the
    // presence of mini-chunks 11/12/13 in 0x1001. If any of n_rotation_words
    // / n_translation_words / n_scale_words is non-zero, it is FoC. A FoC
    // file may still have all three at zero (degenerate; e.g. an empty
    // pose-only file); detection in that case is from raw_info_payload's
    // mini-chunk inventory (see read_ala implementation).
    bool is_foc = false;
};

// Build the top-level chunk-tree representation of a .ala file. The result
// is suitable for write_chunk_tree(...) -> bytes -> disk. Pure function,
// no I/O. Emits the single 0x1000 container with children in the canonical
// order: 0x1001, 0x1002 (per bone), 0x100a (if translation_pool non-empty
// and is_foc), 0x1009 (if rotation_pool non-empty and is_foc).
std::vector<ChunkNode> build_ala(const AlaAnimation& anim);

// Parse a .ala byte buffer into a typed AlaAnimation. Detects EaW vs FoC
// via mini-chunks 11/12/13. Captures raw leaf payloads at every leaf so a
// subsequent build_ala() round-trip is byte-identical. Throws
// std::runtime_error on structurally broken input.
AlaAnimation read_ala(const std::uint8_t* data, std::size_t size);

}  // namespace alamo_format

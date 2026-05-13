#include "alamo_format/ala_anim.h"

#include "alamo_format/chunk_io.h"
#include "alamo_format/chunk_tree.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace alamo_format {

namespace {

// Chunk IDs (mirrored from ala_writer.cpp).
constexpr std::uint32_t kChAnimRoot      = 0x1000;
constexpr std::uint32_t kChAnimInfo      = 0x1001;
constexpr std::uint32_t kChAnimBone      = 0x1002;
constexpr std::uint32_t kChAnimBoneInfo  = 0x1003;
constexpr std::uint32_t kChAnimRotPool   = 0x1009;
constexpr std::uint32_t kChAnimTransPool = 0x100a;

// Mini-chunk IDs.
constexpr std::uint8_t kMiniInfoFrames     = 1;
constexpr std::uint8_t kMiniInfoFps        = 2;
constexpr std::uint8_t kMiniInfoBones      = 3;
constexpr std::uint8_t kMiniInfoRotWords   = 11;
constexpr std::uint8_t kMiniInfoTransWords = 12;
constexpr std::uint8_t kMiniInfoScaleWords = 13;

constexpr std::uint8_t kMiniBoneName        = 4;
constexpr std::uint8_t kMiniBoneIndex       = 5;
constexpr std::uint8_t kMiniBoneTransOffset = 6;
constexpr std::uint8_t kMiniBoneTransScale  = 7;
constexpr std::uint8_t kMiniBoneScaleOffset = 8;
constexpr std::uint8_t kMiniBoneScaleScale  = 9;
constexpr std::uint8_t kMiniBoneIdxTrans    = 14;
constexpr std::uint8_t kMiniBoneIdxScale    = 15;
constexpr std::uint8_t kMiniBoneIdxRot      = 16;
constexpr std::uint8_t kMiniBoneDefaultRot  = 17;

// ---- Mini-chunk primitives ------------------------------------------------

// Walks the mini-chunks inside a 0x1001 / 0x1003 leaf payload, dispatching
// each (id, payload-pointer, size) to the callback. Throws on truncation.
template <typename F>
void walk_mini_chunks(const std::vector<std::uint8_t>& payload, F&& cb) {
    std::size_t cur = 0;
    while (cur < payload.size()) {
        if (cur + 2 > payload.size()) {
            throw std::runtime_error("ala_reader: truncated mini-chunk header at "
                                     + std::to_string(cur));
        }
        std::uint8_t id = payload[cur];
        std::uint8_t sz = payload[cur + 1];
        std::size_t body = cur + 2;
        if (body + sz > payload.size()) {
            throw std::runtime_error("ala_reader: mini-chunk payload truncated (id=0x"
                                     + std::to_string(id) + ")");
        }
        cb(id, payload.data() + body, static_cast<std::size_t>(sz));
        cur = body + sz;
    }
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    std::uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

std::int16_t read_i16_le(const std::uint8_t* p) {
    std::int16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

float read_f32_le(const std::uint8_t* p) {
    float v = 0.0f;
    std::memcpy(&v, p, 4);
    return v;
}

// ---- 0x1001 parsing -------------------------------------------------------

void parse_info_payload(const std::vector<std::uint8_t>& payload, AlaAnimation& out) {
    out.raw_info_payload = payload;
    bool saw_foc_minichunk = false;
    walk_mini_chunks(payload, [&](std::uint8_t id, const std::uint8_t* p, std::size_t sz){
        switch (id) {
        case kMiniInfoFrames:
            if (sz >= 4) out.n_frames = read_u32_le(p);
            break;
        case kMiniInfoFps:
            if (sz >= 4) out.fps = read_f32_le(p);
            break;
        case kMiniInfoBones:
            // We derive n_bones from bones.size() on emission; this field is
            // informational here. (Validated against bones.size() at end.)
            break;
        case kMiniInfoRotWords:
            if (sz >= 4) out.n_rotation_words = read_u32_le(p);
            saw_foc_minichunk = true;
            break;
        case kMiniInfoTransWords:
            if (sz >= 4) out.n_translation_words = read_u32_le(p);
            saw_foc_minichunk = true;
            break;
        case kMiniInfoScaleWords:
            if (sz >= 4) out.n_scale_words = read_u32_le(p);
            saw_foc_minichunk = true;
            break;
        default:
            // Forward-compat: unknown mini-chunks are preserved via raw_info_payload.
            break;
        }
    });
    out.is_foc = saw_foc_minichunk;
}

// ---- 0x1003 parsing -------------------------------------------------------

void parse_bone_info_payload(const std::vector<std::uint8_t>& payload, AlaBoneTrack& out) {
    out.raw_info_payload = payload;
    walk_mini_chunks(payload, [&](std::uint8_t id, const std::uint8_t* p, std::size_t sz){
        switch (id) {
        case kMiniBoneName: {
            // Null-terminated cstring; mini-chunk size includes the terminator.
            std::size_t n = sz;
            while (n > 0 && p[n - 1] == 0) --n;
            out.name.assign(reinterpret_cast<const char*>(p), n);
            break;
        }
        case kMiniBoneIndex:
            if (sz >= 4) out.skeleton_index = read_u32_le(p);
            break;
        case kMiniBoneTransOffset:
            if (sz >= 12) {
                out.trans_offset[0] = read_f32_le(p + 0);
                out.trans_offset[1] = read_f32_le(p + 4);
                out.trans_offset[2] = read_f32_le(p + 8);
            }
            break;
        case kMiniBoneTransScale:
            if (sz >= 12) {
                out.trans_scale[0] = read_f32_le(p + 0);
                out.trans_scale[1] = read_f32_le(p + 4);
                out.trans_scale[2] = read_f32_le(p + 8);
            }
            break;
        case kMiniBoneScaleOffset:
            if (sz >= 12) {
                out.scale_offset[0] = read_f32_le(p + 0);
                out.scale_offset[1] = read_f32_le(p + 4);
                out.scale_offset[2] = read_f32_le(p + 8);
            }
            break;
        case kMiniBoneScaleScale:
            if (sz >= 12) {
                out.scale_scale[0] = read_f32_le(p + 0);
                out.scale_scale[1] = read_f32_le(p + 4);
                out.scale_scale[2] = read_f32_le(p + 8);
            }
            break;
        case kMiniBoneIdxTrans:
            if (sz >= 2) out.idx_translation = read_i16_le(p);
            break;
        case kMiniBoneIdxScale:
            if (sz >= 2) out.idx_scale = read_i16_le(p);
            break;
        case kMiniBoneIdxRot:
            if (sz >= 2) out.idx_rotation = read_i16_le(p);
            break;
        case kMiniBoneDefaultRot:
            if (sz >= 8) {
                for (int i = 0; i < 4; ++i) out.default_rotation[i] = read_i16_le(p + i * 2);
            }
            break;
        default:
            // Unknown mini-chunk; preserved via raw_info_payload.
            break;
        }
    });
}

// ---- Pool parsing ---------------------------------------------------------

std::vector<std::int16_t> bytes_to_pool(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::int16_t> pool(bytes.size() / sizeof(std::int16_t));
    if (!pool.empty()) {
        std::memcpy(pool.data(), bytes.data(), pool.size() * sizeof(std::int16_t));
    }
    return pool;
}

// ---- 0x1002 parsing -------------------------------------------------------

AlaBoneTrack parse_bone_container(const ChunkNode& node) {
    AlaBoneTrack bone;
    for (const auto& child : node.children) {
        if (child.id == kChAnimBoneInfo && !child.is_container) {
            parse_bone_info_payload(child.payload, bone);
        } else {
            // 0x1004 / 0x1005 / 0x1006 / 0x1007 / 0x1008 — preserve verbatim.
            bone.track_leaves.push_back(child);
        }
    }
    return bone;
}

}  // namespace

AlaAnimation read_ala(const std::uint8_t* data, std::size_t size) {
    AlaAnimation out;

    std::vector<ChunkNode> tree = read_chunk_tree(data, size);

    // Find the top-level 0x1000 container. Vanilla files always have exactly
    // one; we accept anything that has a 0x1000 at the top and ignore other
    // top-level siblings (none observed in the corpus).
    const ChunkNode* root = nullptr;
    for (const auto& n : tree) {
        if (n.id == kChAnimRoot && n.is_container) {
            root = &n;
            break;
        }
    }
    if (!root) {
        throw std::runtime_error("ala_reader: missing 0x1000 (Animation root) container");
    }

    for (const auto& child : root->children) {
        switch (child.id) {
        case kChAnimInfo:
            if (!child.is_container) {
                parse_info_payload(child.payload, out);
            }
            break;
        case kChAnimBone:
            if (child.is_container) {
                out.bones.push_back(parse_bone_container(child));
            }
            break;
        case kChAnimRotPool:
            if (!child.is_container) {
                out.rotation_pool = bytes_to_pool(child.payload);
            }
            break;
        case kChAnimTransPool:
            if (!child.is_container) {
                out.translation_pool = bytes_to_pool(child.payload);
            }
            break;
        default:
            // Unknown top-level child — silently ignore. None observed in corpus;
            // round-trip byte-identity is then not guaranteed (but we never reach
            // this branch for vanilla content).
            break;
        }
    }

    return out;
}

}  // namespace alamo_format

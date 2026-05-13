#include "alamo_format/ala_anim.h"

#include "alamo_format/chunk_io.h"

#include <cstring>

namespace alamo_format {

namespace {

// ---- Top-level chunk IDs --------------------------------------------------
constexpr std::uint32_t kChAnimRoot          = 0x1000;
constexpr std::uint32_t kChAnimInfo          = 0x1001;
constexpr std::uint32_t kChAnimBone          = 0x1002;
constexpr std::uint32_t kChAnimBoneInfo      = 0x1003;
// 0x1004 / 0x1005 / 0x1006 / 0x1007 / 0x1008 pass through as opaque
// track_leaves on AlaBoneTrack; build_ala emits them verbatim.
constexpr std::uint32_t kChAnimRotPool       = 0x1009;
constexpr std::uint32_t kChAnimTransPool     = 0x100a;

// ---- 0x1001 mini-chunks ---------------------------------------------------
constexpr std::uint8_t kMiniInfoFrames        = 1;
constexpr std::uint8_t kMiniInfoFps           = 2;
constexpr std::uint8_t kMiniInfoBones         = 3;
constexpr std::uint8_t kMiniInfoRotWords      = 11;
constexpr std::uint8_t kMiniInfoTransWords    = 12;
constexpr std::uint8_t kMiniInfoScaleWords    = 13;

// ---- 0x1003 mini-chunks ---------------------------------------------------
constexpr std::uint8_t kMiniBoneName          = 4;
constexpr std::uint8_t kMiniBoneIndex         = 5;
constexpr std::uint8_t kMiniBoneTransOffset   = 6;
constexpr std::uint8_t kMiniBoneTransScale    = 7;
constexpr std::uint8_t kMiniBoneScaleOffset   = 8;
constexpr std::uint8_t kMiniBoneScaleScale    = 9;
constexpr std::uint8_t kMiniBoneIdxTrans      = 14;
constexpr std::uint8_t kMiniBoneIdxScale      = 15;
constexpr std::uint8_t kMiniBoneIdxRot        = 16;
constexpr std::uint8_t kMiniBoneDefaultRot    = 17;

// ---- Helpers (mirror alo_build.cpp patterns) -----------------------------

ChunkNode make_leaf(std::uint32_t id, std::vector<std::uint8_t> payload) {
    ChunkNode n;
    n.id = id;
    n.is_container = false;
    n.payload = std::move(payload);
    return n;
}

ChunkNode make_container(std::uint32_t id, std::vector<ChunkNode> children) {
    ChunkNode n;
    n.id = id;
    n.is_container = true;
    n.children = std::move(children);
    return n;
}

void append_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    const std::size_t off = v.size();
    v.resize(off + 4);
    std::memcpy(v.data() + off, &x, 4);
}

void append_i16(std::vector<std::uint8_t>& v, std::int16_t x) {
    const std::size_t off = v.size();
    v.resize(off + 2);
    std::memcpy(v.data() + off, &x, 2);
}

void append_f32(std::vector<std::uint8_t>& v, float x) {
    const std::size_t off = v.size();
    v.resize(off + 4);
    std::memcpy(v.data() + off, &x, 4);
}

void append_cstring(std::vector<std::uint8_t>& v, const std::string& s) {
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
}

// Mini-chunk: 2-byte header [id, size] then payload. The caller passes a
// callable that appends the payload bytes to `v`; we record the payload
// start, run the callable, then back-patch the size byte.
template <typename F>
void emit_mini(std::vector<std::uint8_t>& v, std::uint8_t mini_id, F&& fill) {
    v.push_back(mini_id);
    const std::size_t size_off = v.size();
    v.push_back(0);  // placeholder size; patched after fill
    const std::size_t payload_start = v.size();
    fill(v);
    const std::size_t payload_size = v.size() - payload_start;
    // mini-chunk size is a single byte. Payloads > 255 cannot be encoded here;
    // such payloads belong in a full chunk, not a mini-chunk. The 0x1001 and
    // 0x1003 mini-chunks we synthesise never exceed this (longest is the bone
    // name string, which is the same string the .alo writer caps similarly).
    v[size_off] = static_cast<std::uint8_t>(payload_size);
}

// ---- 0x1001 (animation info) synthesis -----------------------------------

std::vector<std::uint8_t> synth_info_payload(const AlaAnimation& anim) {
    std::vector<std::uint8_t> p;
    emit_mini(p, kMiniInfoFrames, [&](auto& v){ append_u32(v, anim.n_frames); });
    emit_mini(p, kMiniInfoFps,    [&](auto& v){ append_f32(v, anim.fps); });
    emit_mini(p, kMiniInfoBones,  [&](auto& v){
        append_u32(v, static_cast<std::uint32_t>(anim.bones.size()));
    });
    if (anim.is_foc) {
        emit_mini(p, kMiniInfoRotWords,   [&](auto& v){ append_u32(v, anim.n_rotation_words); });
        emit_mini(p, kMiniInfoTransWords, [&](auto& v){ append_u32(v, anim.n_translation_words); });
        emit_mini(p, kMiniInfoScaleWords, [&](auto& v){ append_u32(v, anim.n_scale_words); });
    }
    return p;
}

// ---- 0x1003 (per-bone info) synthesis ------------------------------------

std::vector<std::uint8_t> synth_bone_info_payload(const AlaBoneTrack& bone, bool is_foc) {
    std::vector<std::uint8_t> p;
    emit_mini(p, kMiniBoneName, [&](auto& v){ append_cstring(v, bone.name); });
    emit_mini(p, kMiniBoneIndex, [&](auto& v){ append_u32(v, bone.skeleton_index); });
    emit_mini(p, kMiniBoneTransOffset, [&](auto& v){
        for (float c : bone.trans_offset) append_f32(v, c);
    });
    emit_mini(p, kMiniBoneTransScale, [&](auto& v){
        for (float c : bone.trans_scale) append_f32(v, c);
    });
    emit_mini(p, kMiniBoneScaleOffset, [&](auto& v){
        for (float c : bone.scale_offset) append_f32(v, c);
    });
    emit_mini(p, kMiniBoneScaleScale, [&](auto& v){
        for (float c : bone.scale_scale) append_f32(v, c);
    });
    if (is_foc) {
        emit_mini(p, kMiniBoneIdxTrans, [&](auto& v){ append_i16(v, bone.idx_translation); });
        emit_mini(p, kMiniBoneIdxScale, [&](auto& v){ append_i16(v, bone.idx_scale); });
        emit_mini(p, kMiniBoneIdxRot,   [&](auto& v){ append_i16(v, bone.idx_rotation); });
        emit_mini(p, kMiniBoneDefaultRot, [&](auto& v){
            for (std::int16_t c : bone.default_rotation) append_i16(v, c);
        });
    }
    return p;
}

// ---- File-scope pool serialisation ---------------------------------------

std::vector<std::uint8_t> pool_to_bytes(const std::vector<std::int16_t>& pool) {
    std::vector<std::uint8_t> p(pool.size() * sizeof(std::int16_t));
    if (!pool.empty()) {
        std::memcpy(p.data(), pool.data(), p.size());
    }
    return p;
}

}  // namespace

std::vector<ChunkNode> build_ala(const AlaAnimation& anim) {
    std::vector<ChunkNode> root_kids;

    // 0x1001 — info leaf
    std::vector<std::uint8_t> info_payload =
        anim.raw_info_payload.empty()
            ? synth_info_payload(anim)
            : anim.raw_info_payload;
    root_kids.push_back(make_leaf(kChAnimInfo, std::move(info_payload)));

    // 0x1002 — one container per bone
    for (const auto& bone : anim.bones) {
        std::vector<ChunkNode> bone_kids;

        std::vector<std::uint8_t> bone_info_payload =
            bone.raw_info_payload.empty()
                ? synth_bone_info_payload(bone, anim.is_foc)
                : bone.raw_info_payload;
        bone_kids.push_back(make_leaf(kChAnimBoneInfo, std::move(bone_info_payload)));

        // 0x1004 / 0x1005 / 0x1006 / 0x1007 / 0x1008 pass through verbatim.
        for (const auto& leaf : bone.track_leaves) {
            bone_kids.push_back(leaf);
        }

        root_kids.push_back(make_container(kChAnimBone, std::move(bone_kids)));
    }

    // FoC file-scope pools. Order per vanilla corpus: 0x100a then 0x1009.
    if (anim.is_foc) {
        if (!anim.translation_pool.empty()) {
            root_kids.push_back(make_leaf(kChAnimTransPool,
                                          pool_to_bytes(anim.translation_pool)));
        }
        if (!anim.rotation_pool.empty()) {
            root_kids.push_back(make_leaf(kChAnimRotPool,
                                          pool_to_bytes(anim.rotation_pool)));
        }
    }

    // Wrap everything in the single top-level 0x1000 container.
    std::vector<ChunkNode> out;
    out.push_back(make_container(kChAnimRoot, std::move(root_kids)));
    return out;
}

}  // namespace alamo_format

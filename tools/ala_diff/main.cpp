// ala_diff: typed, semantic comparator and inspector for .ala animations.
//
// Phase 14 diagnostic harness. The Phase 8a typed pipeline (read_ala ->
// AlaAnimation -> build_ala) is byte-identity-preserving by design, but
// once two .ala files come from DIFFERENT sources (vanilla + re-export,
// or two different Max scenes) a raw byte diff is dominated by legitimate
// noise: per-bone trans_offset/trans_scale recompute from per-clip
// min/max, so any sampling-precision delta cascades into the whole pool.
//
// ala_diff parses both files via read_ala, then compares the typed view:
//   - header (frame count, fps, bone count, is_foc, pool sizes)
//   - per-bone metadata (name, skeleton_index, idx_*, default_rotation,
//     trans_offset/scale) -- bones are matched by NAME, not index
//   - per-frame UNPACKED rotation quaternion (sign-aligned via dot
//     product before epsilon compare) and translation (per-axis)
//
// Modes:
//   ala_diff --dump <file>     human-readable typed dump (header + per
//                              bone + first/last-frame unpacked tracks)
//   ala_diff <a> <b>           semantic diff; first 5 divergences per
//                              category, exit 1 on any mismatch
//
// Exit: 0 = identical-within-epsilon (or dump succeeded), 1 otherwise.

#include "alamo_format/ala_anim.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr float kQuatEps  = 1.0e-4f;  // ~ 3 LSB at 1/32767 scale
constexpr float kTransEps = 1.0e-3f;  // per-axis epsilon (scene units)
constexpr float kFloatEps = 1.0e-5f;  // bone-metadata floats

constexpr std::size_t kMaxDivergencesShown = 5;

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    f.unsetf(std::ios::skipws);
    return { std::istreambuf_iterator<char>(f),
             std::istreambuf_iterator<char>() };
}

struct Quat4 { float x, y, z, w; };
struct Vec3  { float x, y, z; };

Quat4 unpack_quat(const std::int16_t* p) {
    return {
        static_cast<float>(p[0]) / 32767.0f,
        static_cast<float>(p[1]) / 32767.0f,
        static_cast<float>(p[2]) / 32767.0f,
        static_cast<float>(p[3]) / 32767.0f,
    };
}

Vec3 unpack_trans(const std::int16_t* p,
                  const float offset[3], const float scale[3]) {
    // Translation pool stores uint16 values via memcpy through int16
    // (see pack_translation_uint16 in scene_walker.cpp). Re-read as
    // uint16 before scaling.
    std::uint16_t u[3];
    std::memcpy(u, p, sizeof(u));
    return {
        static_cast<float>(u[0]) * scale[0] + offset[0],
        static_cast<float>(u[1]) * scale[1] + offset[1],
        static_cast<float>(u[2]) * scale[2] + offset[2],
    };
}

float quat_dot(const Quat4& a, const Quat4& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
}

// Returns the larger of |a-b|, |a-(-b)| -- i.e., sign-aligned distance.
// Two quaternions q and -q represent the same rotation, so the diff
// must canonicalise before comparing.
float quat_aligned_max_abs_diff(const Quat4& a, const Quat4& b) {
    const float d = quat_dot(a, b);
    Quat4 bs = (d < 0.0f) ? Quat4{-b.x, -b.y, -b.z, -b.w} : b;
    const float dx = std::fabs(a.x - bs.x);
    const float dy = std::fabs(a.y - bs.y);
    const float dz = std::fabs(a.z - bs.z);
    const float dw = std::fabs(a.w - bs.w);
    return std::max(std::max(dx, dy), std::max(dz, dw));
}

float vec3_max_abs_diff(const Vec3& a, const Vec3& b) {
    return std::max(std::max(std::fabs(a.x - b.x), std::fabs(a.y - b.y)),
                    std::fabs(a.z - b.z));
}

// ---- Dump mode -----------------------------------------------------------

void print_quat_packed(const std::array<std::int16_t, 4>& q) {
    std::printf("[%6d %6d %6d %6d]", q[0], q[1], q[2], q[3]);
}

void print_vec3f(const float v[3]) {
    std::printf("(% .6f % .6f % .6f)", v[0], v[1], v[2]);
}

void dump_anim(const std::string& path, const alamo_format::AlaAnimation& a) {
    std::printf("==== %s ====\n", path.c_str());
    std::printf("n_frames           = %u\n", a.n_frames);
    std::printf("fps                = %.4f\n", a.fps);
    std::printf("n_bones            = %zu\n", a.bones.size());
    std::printf("is_foc             = %s\n", a.is_foc ? "true" : "false");
    std::printf("n_rotation_words   = %u   (pool size = %zu int16)\n",
                a.n_rotation_words, a.rotation_pool.size());
    std::printf("n_translation_words= %u   (pool size = %zu int16)\n",
                a.n_translation_words, a.translation_pool.size());
    std::printf("n_scale_words      = %u\n", a.n_scale_words);
    std::printf("\n");

    for (std::size_t i = 0; i < a.bones.size(); ++i) {
        const auto& b = a.bones[i];
        std::printf("Bone[%zu] %s   skel_idx=%u  idx_rot=%d idx_trans=%d idx_scale=%d\n",
                    i, b.name.c_str(), b.skeleton_index,
                    int(b.idx_rotation), int(b.idx_translation), int(b.idx_scale));
        std::printf("  trans_offset = "); print_vec3f(b.trans_offset); std::printf("\n");
        std::printf("  trans_scale  = "); print_vec3f(b.trans_scale);  std::printf("\n");
        std::printf("  default_rot  = "); print_quat_packed(b.default_rotation);
        Quat4 dq = unpack_quat(b.default_rotation.data());
        std::printf("  -> (% .6f % .6f % .6f % .6f)\n", dq.x, dq.y, dq.z, dq.w);

        // Sampled rotation frames (first, last, midpoint).
        if (b.idx_rotation >= 0 && a.n_rotation_words > 0 &&
            !a.rotation_pool.empty()) {
            const std::size_t stride = a.n_rotation_words;
            const std::size_t k      = static_cast<std::size_t>(b.idx_rotation);
            auto print_frame = [&](const char* label, std::size_t f) {
                if (f >= a.n_frames) return;
                const std::int16_t* p =
                    a.rotation_pool.data() + f * stride + k;
                Quat4 q = unpack_quat(p);
                std::printf("  rot @ %s frame %zu: [%6d %6d %6d %6d] "
                            "-> (% .6f % .6f % .6f % .6f)\n",
                            label, f, p[0], p[1], p[2], p[3],
                            q.x, q.y, q.z, q.w);
            };
            print_frame("first", 0);
            if (a.n_frames > 2) print_frame("  mid", a.n_frames / 2);
            if (a.n_frames > 1) print_frame(" last", a.n_frames - 1);
        }

        // Sampled translation frames (first, last, midpoint).
        if (b.idx_translation >= 0 && a.n_translation_words > 0 &&
            !a.translation_pool.empty()) {
            const std::size_t stride = a.n_translation_words;
            const std::size_t k      = static_cast<std::size_t>(b.idx_translation);
            auto print_frame = [&](const char* label, std::size_t f) {
                if (f >= a.n_frames) return;
                const std::int16_t* p =
                    a.translation_pool.data() + f * stride + k;
                Vec3 v = unpack_trans(p, b.trans_offset, b.trans_scale);
                std::uint16_t u[3];
                std::memcpy(u, p, sizeof(u));
                std::printf("  trn @ %s frame %zu: [%5u %5u %5u] "
                            "-> (% .6f % .6f % .6f)\n",
                            label, f, u[0], u[1], u[2], v.x, v.y, v.z);
            };
            print_frame("first", 0);
            if (a.n_frames > 2) print_frame("  mid", a.n_frames / 2);
            if (a.n_frames > 1) print_frame(" last", a.n_frames - 1);
        }

        if (!b.track_leaves.empty()) {
            std::printf("  per-bone track leaves (0x1004/1005/1006/1007/1008):\n");
            for (const auto& leaf : b.track_leaves) {
                std::printf("    0x%04X (%zu bytes)\n",
                            leaf.id, leaf.payload.size());
            }
        }
        std::printf("\n");
    }
}

// ---- Diff mode -----------------------------------------------------------

struct Counter {
    const char* category;
    std::size_t shown = 0;
    std::size_t total = 0;
    bool note(const char* fmt, ...) {
        ++total;
        if (shown >= kMaxDivergencesShown) return false;
        ++shown;
        std::printf("  [%s] ", category);
        std::va_list ap;
        va_start(ap, fmt);
        std::vprintf(fmt, ap);
        va_end(ap);
        std::printf("\n");
        return true;
    }
};

bool floats_diff(const float* a, const float* b, std::size_t n, float eps) {
    for (std::size_t i = 0; i < n; ++i) {
        if (std::fabs(a[i] - b[i]) > eps) return true;
    }
    return false;
}

int diff_anims(const std::string& path_a, const std::string& path_b,
               const alamo_format::AlaAnimation& a,
               const alamo_format::AlaAnimation& b) {
    std::printf("==== diff %s vs %s ====\n", path_a.c_str(), path_b.c_str());
    bool any_diff = false;

    // --- Header --------------------------------------------------------
    Counter c_hdr{"header"};
    if (a.n_frames != b.n_frames) {
        c_hdr.note("n_frames: A=%u  B=%u", a.n_frames, b.n_frames);
        any_diff = true;
    }
    if (std::fabs(a.fps - b.fps) > kFloatEps) {
        c_hdr.note("fps: A=%.4f  B=%.4f", a.fps, b.fps);
        any_diff = true;
    }
    if (a.bones.size() != b.bones.size()) {
        c_hdr.note("n_bones: A=%zu  B=%zu", a.bones.size(), b.bones.size());
        any_diff = true;
    }
    if (a.is_foc != b.is_foc) {
        c_hdr.note("is_foc: A=%d  B=%d", int(a.is_foc), int(b.is_foc));
        any_diff = true;
    }
    if (a.n_rotation_words != b.n_rotation_words) {
        c_hdr.note("n_rotation_words: A=%u  B=%u",
                   a.n_rotation_words, b.n_rotation_words);
        any_diff = true;
    }
    if (a.n_translation_words != b.n_translation_words) {
        c_hdr.note("n_translation_words: A=%u  B=%u",
                   a.n_translation_words, b.n_translation_words);
        any_diff = true;
    }
    if (a.n_scale_words != b.n_scale_words) {
        c_hdr.note("n_scale_words: A=%u  B=%u",
                   a.n_scale_words, b.n_scale_words);
        any_diff = true;
    }

    // --- Bone presence (matched by name) -------------------------------
    std::map<std::string, std::size_t> a_idx, b_idx;
    for (std::size_t i = 0; i < a.bones.size(); ++i) a_idx[a.bones[i].name] = i;
    for (std::size_t i = 0; i < b.bones.size(); ++i) b_idx[b.bones[i].name] = i;

    Counter c_pres{"bone-presence"};
    for (const auto& [name, _] : a_idx) {
        if (b_idx.find(name) == b_idx.end()) {
            c_pres.note("bone '%s' present in A, missing in B", name.c_str());
            any_diff = true;
        }
    }
    for (const auto& [name, _] : b_idx) {
        if (a_idx.find(name) == a_idx.end()) {
            c_pres.note("bone '%s' present in B, missing in A", name.c_str());
            any_diff = true;
        }
    }

    // Bones present in both: compare metadata + per-frame tracks.
    Counter c_meta{"bone-meta"};
    Counter c_rot {"rotation-frame"};
    Counter c_trn {"translation-frame"};
    const std::uint32_t common_frames = std::min(a.n_frames, b.n_frames);

    for (const auto& [name, ai] : a_idx) {
        auto bit = b_idx.find(name);
        if (bit == b_idx.end()) continue;
        const std::size_t bi = bit->second;
        const auto& ba = a.bones[ai];
        const auto& bb = b.bones[bi];

        if (ba.skeleton_index != bb.skeleton_index) {
            c_meta.note("'%s' skeleton_index: A=%u  B=%u",
                        name.c_str(), ba.skeleton_index, bb.skeleton_index);
            any_diff = true;
        }
        if (ba.idx_rotation != bb.idx_rotation) {
            c_meta.note("'%s' idx_rotation: A=%d  B=%d",
                        name.c_str(), int(ba.idx_rotation), int(bb.idx_rotation));
            any_diff = true;
        }
        if (ba.idx_translation != bb.idx_translation) {
            c_meta.note("'%s' idx_translation: A=%d  B=%d",
                        name.c_str(), int(ba.idx_translation),
                        int(bb.idx_translation));
            any_diff = true;
        }

        // default_rotation: unpack to floats, sign-align, then compare.
        Quat4 da = unpack_quat(ba.default_rotation.data());
        Quat4 db = unpack_quat(bb.default_rotation.data());
        if (quat_aligned_max_abs_diff(da, db) > kQuatEps) {
            c_meta.note("'%s' default_rotation differs: "
                        "A=(% .4f % .4f % .4f % .4f)  B=(% .4f % .4f % .4f % .4f)",
                        name.c_str(),
                        da.x, da.y, da.z, da.w,
                        db.x, db.y, db.z, db.w);
            any_diff = true;
        }

        // trans_offset / trans_scale -- per-bone packing parameters.
        // Different ranges of motion legitimately produce different
        // offset/scale, but if both bones have tracks, the UNPACKED
        // per-frame positions should agree -- so flag offset/scale
        // mismatch only as informational.
        if (floats_diff(ba.trans_offset, bb.trans_offset, 3, kFloatEps)) {
            c_meta.note("'%s' trans_offset: A=(% .4f % .4f % .4f) "
                        "B=(% .4f % .4f % .4f)   (informational; "
                        "unpacked positions are the source of truth)",
                        name.c_str(),
                        ba.trans_offset[0], ba.trans_offset[1], ba.trans_offset[2],
                        bb.trans_offset[0], bb.trans_offset[1], bb.trans_offset[2]);
        }

        // Per-frame rotation tracks.
        if (ba.idx_rotation >= 0 && bb.idx_rotation >= 0 &&
            !a.rotation_pool.empty() && !b.rotation_pool.empty()) {
            const std::size_t sa = a.n_rotation_words;
            const std::size_t sb = b.n_rotation_words;
            const std::size_t ka = static_cast<std::size_t>(ba.idx_rotation);
            const std::size_t kb = static_cast<std::size_t>(bb.idx_rotation);
            for (std::uint32_t f = 0; f < common_frames; ++f) {
                const std::int16_t* pa = a.rotation_pool.data() + f*sa + ka;
                const std::int16_t* pb = b.rotation_pool.data() + f*sb + kb;
                Quat4 qa = unpack_quat(pa);
                Quat4 qb = unpack_quat(pb);
                if (quat_aligned_max_abs_diff(qa, qb) > kQuatEps) {
                    c_rot.note("'%s' frame %u: "
                               "A=(% .4f % .4f % .4f % .4f) "
                               "B=(% .4f % .4f % .4f % .4f)",
                               name.c_str(), f,
                               qa.x, qa.y, qa.z, qa.w,
                               qb.x, qb.y, qb.z, qb.w);
                    any_diff = true;
                }
            }
        }

        // Per-frame translation tracks.
        if (ba.idx_translation >= 0 && bb.idx_translation >= 0 &&
            !a.translation_pool.empty() && !b.translation_pool.empty()) {
            const std::size_t sa = a.n_translation_words;
            const std::size_t sb = b.n_translation_words;
            const std::size_t ka = static_cast<std::size_t>(ba.idx_translation);
            const std::size_t kb = static_cast<std::size_t>(bb.idx_translation);
            for (std::uint32_t f = 0; f < common_frames; ++f) {
                const std::int16_t* pa = a.translation_pool.data() + f*sa + ka;
                const std::int16_t* pb = b.translation_pool.data() + f*sb + kb;
                Vec3 va = unpack_trans(pa, ba.trans_offset, ba.trans_scale);
                Vec3 vb = unpack_trans(pb, bb.trans_offset, bb.trans_scale);
                if (vec3_max_abs_diff(va, vb) > kTransEps) {
                    c_trn.note("'%s' frame %u: A=(% .4f % .4f % .4f) "
                               "B=(% .4f % .4f % .4f)",
                               name.c_str(), f,
                               va.x, va.y, va.z, vb.x, vb.y, vb.z);
                    any_diff = true;
                }
            }
        }
    }

    auto summary = [](const Counter& c) {
        if (c.total > 0) {
            std::printf("  -> %s: %zu total divergence%s (%zu shown)\n",
                        c.category, c.total, c.total == 1 ? "" : "s",
                        c.shown);
        }
    };
    summary(c_hdr);
    summary(c_pres);
    summary(c_meta);
    summary(c_rot);
    summary(c_trn);

    if (!any_diff) {
        std::printf("  EQUIVALENT (within epsilon)\n");
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--dump") {
        try {
            auto bytes = read_file(argv[2]);
            auto a = alamo_format::read_ala(bytes.data(), bytes.size());
            dump_anim(argv[2], a);
            return EXIT_SUCCESS;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ala_diff: %s\n", e.what());
            return EXIT_FAILURE;
        }
    }
    if (argc == 3) {
        try {
            auto bytes_a = read_file(argv[1]);
            auto bytes_b = read_file(argv[2]);
            auto a = alamo_format::read_ala(bytes_a.data(), bytes_a.size());
            auto b = alamo_format::read_ala(bytes_b.data(), bytes_b.size());
            return diff_anims(argv[1], argv[2], a, b);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ala_diff: %s\n", e.what());
            return EXIT_FAILURE;
        }
    }
    std::fprintf(stderr,
        "usage:\n"
        "  ala_diff --dump <file.ala>     human-readable typed dump\n"
        "  ala_diff <a.ala> <b.ala>       semantic diff (exit 0 = equivalent)\n");
    return EXIT_FAILURE;
}

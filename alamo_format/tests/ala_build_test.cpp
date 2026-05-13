// Phase 8a unit tests for the typed .ala read/write pipeline.
//
// Coverage:
//   - build_ala synthesises canonical bytes from typed fields (no raw payloads)
//   - read_ala -> build_ala -> bytes is byte-identical for synthesised inputs
//   - read_ala captures raw_info_payload for round-trip preservation
//   - FoC detection via mini-chunks 11/12/13 presence
//   - EaW (no FoC mini-chunks) round-trips track_leaves verbatim
//   - Pool int16 streams survive round-trip
//   - Visibility track leaves pass through (encoding is not validated here;
//     that's an 8d concern -- 8a just preserves bytes)
//   - Edge cases: empty animations, bones with no FoC indices

#include <catch2/catch_test_macros.hpp>

#include "alamo_format/ala_anim.h"
#include "alamo_format/chunk_tree.h"

#include <cstring>

using namespace alamo_format;

namespace {

ChunkNode make_leaf(std::uint32_t id, std::vector<std::uint8_t> bytes) {
    ChunkNode n;
    n.id = id;
    n.is_container = false;
    n.payload = std::move(bytes);
    return n;
}

AlaAnimation minimal_foc(std::uint32_t n_frames = 30) {
    AlaAnimation a;
    a.is_foc            = true;
    a.n_frames          = n_frames;
    a.fps               = 30.0f;
    a.n_rotation_words  = 0;
    a.n_translation_words = 0;
    a.n_scale_words     = 0;
    return a;
}

AlaBoneTrack minimal_bone(const std::string& name, std::uint32_t idx) {
    AlaBoneTrack b;
    b.name = name;
    b.skeleton_index = idx;
    b.trans_offset[0] = 0.0f; b.trans_offset[1] = 0.0f; b.trans_offset[2] = 0.0f;
    b.trans_scale[0]  = 0.0f; b.trans_scale[1]  = 0.0f; b.trans_scale[2]  = 0.0f;
    b.scale_offset[0] = 1.0f; b.scale_offset[1] = 1.0f; b.scale_offset[2] = 1.0f;
    b.scale_scale[0]  = 0.0f; b.scale_scale[1]  = 0.0f; b.scale_scale[2]  = 0.0f;
    b.idx_translation = -1;
    b.idx_scale       = -1;
    b.idx_rotation    = -1;
    b.default_rotation = {0, 0, 0, 32767};
    return b;
}

std::vector<std::uint8_t> tree_to_bytes(const std::vector<ChunkNode>& nodes) {
    return write_chunk_tree(nodes);
}

}  // namespace

TEST_CASE("build_ala on empty animation emits just 0x1000 wrapping 0x1001") {
    AlaAnimation a = minimal_foc(/*n_frames=*/0);
    auto tree = build_ala(a);
    REQUIRE(tree.size() == 1);
    REQUIRE(tree[0].id == 0x1000);
    REQUIRE(tree[0].is_container);
    REQUIRE(tree[0].children.size() == 1);
    REQUIRE(tree[0].children[0].id == 0x1001);
    REQUIRE_FALSE(tree[0].children[0].is_container);
}

TEST_CASE("0x1001 synthesised payload has mini-chunks 1/2/3 (and 11/12/13 in FoC)") {
    AlaAnimation a = minimal_foc(60);
    a.n_rotation_words = 4;
    a.n_translation_words = 3;
    a.n_scale_words = 0;

    auto tree = build_ala(a);
    const auto& info = tree[0].children[0];
    const auto& p = info.payload;

    // Mini-chunk walk: each mini = [u8 id, u8 size, ... bytes].
    std::size_t cur = 0;
    std::vector<std::uint8_t> seen_ids;
    while (cur < p.size()) {
        seen_ids.push_back(p[cur]);
        cur += 2 + p[cur + 1];
    }
    REQUIRE(seen_ids == std::vector<std::uint8_t>{1, 2, 3, 11, 12, 13});
}

TEST_CASE("EaW-flavour info (is_foc=false) skips mini-chunks 11/12/13") {
    AlaAnimation a;
    a.is_foc = false;
    a.n_frames = 24;
    a.fps = 30.0f;
    auto tree = build_ala(a);
    const auto& p = tree[0].children[0].payload;
    std::size_t cur = 0;
    std::vector<std::uint8_t> ids;
    while (cur < p.size()) {
        ids.push_back(p[cur]);
        cur += 2 + p[cur + 1];
    }
    REQUIRE(ids == std::vector<std::uint8_t>{1, 2, 3});
}

TEST_CASE("Single bone with no tracks: 0x1002 contains exactly one 0x1003") {
    AlaAnimation a = minimal_foc(10);
    a.bones.push_back(minimal_bone("Root", 0));
    auto tree = build_ala(a);
    REQUIRE(tree[0].children.size() == 2);
    REQUIRE(tree[0].children[1].id == 0x1002);
    REQUIRE(tree[0].children[1].is_container);
    REQUIRE(tree[0].children[1].children.size() == 1);
    REQUIRE(tree[0].children[1].children[0].id == 0x1003);
}

TEST_CASE("0x1003 synthesised payload has mini-chunks 4..9 and (FoC) 14..17") {
    AlaAnimation a = minimal_foc(10);
    a.bones.push_back(minimal_bone("B1", 5));
    auto tree = build_ala(a);
    const auto& info = tree[0].children[1].children[0];
    const auto& p = info.payload;
    std::size_t cur = 0;
    std::vector<std::uint8_t> ids;
    while (cur < p.size()) {
        ids.push_back(p[cur]);
        cur += 2 + p[cur + 1];
    }
    REQUIRE(ids == std::vector<std::uint8_t>{4, 5, 6, 7, 8, 9, 14, 15, 16, 17});
}

TEST_CASE("track_leaves emit verbatim after 0x1003") {
    AlaAnimation a = minimal_foc(8);
    AlaBoneTrack bone = minimal_bone("B", 0);
    bone.track_leaves.push_back(make_leaf(0x1007, {0xAB, 0xCD, 0xEF}));  // visibility blob
    bone.track_leaves.push_back(make_leaf(0x1008, {0x42}));               // unknown leaf
    a.bones.push_back(std::move(bone));
    auto tree = build_ala(a);
    const auto& bone_kids = tree[0].children[1].children;
    REQUIRE(bone_kids.size() == 3);
    REQUIRE(bone_kids[0].id == 0x1003);
    REQUIRE(bone_kids[1].id == 0x1007);
    REQUIRE(bone_kids[1].payload == std::vector<std::uint8_t>{0xAB, 0xCD, 0xEF});
    REQUIRE(bone_kids[2].id == 0x1008);
    REQUIRE(bone_kids[2].payload == std::vector<std::uint8_t>{0x42});
}

TEST_CASE("FoC pools emit at file scope: 0x100a before 0x1009 (per corpus order)") {
    AlaAnimation a = minimal_foc(4);
    a.n_rotation_words = 4;
    a.n_translation_words = 3;
    a.rotation_pool    = {1, 2, 3, 4};
    a.translation_pool = {10, 20, 30};

    auto tree = build_ala(a);
    const auto& kids = tree[0].children;
    // 0x1001, then 0x100a, then 0x1009 (no bones in this scene).
    REQUIRE(kids.size() == 3);
    REQUIRE(kids[0].id == 0x1001);
    REQUIRE(kids[1].id == 0x100a);
    REQUIRE(kids[2].id == 0x1009);
}

TEST_CASE("FoC pools omitted when corresponding pool is empty") {
    AlaAnimation a = minimal_foc(4);
    a.n_rotation_words = 2;
    a.rotation_pool = {7, 8};
    // translation_pool empty -> no 0x100a chunk.
    auto tree = build_ala(a);
    const auto& kids = tree[0].children;
    REQUIRE(kids.size() == 2);
    REQUIRE(kids[0].id == 0x1001);
    REQUIRE(kids[1].id == 0x1009);
}

TEST_CASE("Pool int16 stream is serialised as little-endian bytes") {
    AlaAnimation a = minimal_foc(1);
    a.n_rotation_words = 0;
    a.n_translation_words = 1;
    a.translation_pool = {static_cast<std::int16_t>(0x1234)};
    auto tree = build_ala(a);
    const auto& pool = tree[0].children[1];
    REQUIRE(pool.id == 0x100a);
    REQUIRE(pool.payload == std::vector<std::uint8_t>{0x34, 0x12});
}

TEST_CASE("read_ala round-trips a synthesised AlaAnimation byte-identically") {
    AlaAnimation src = minimal_foc(15);
    src.n_rotation_words = 8;
    src.n_translation_words = 6;
    src.rotation_pool    = {1, -1, 2, -2, 3, -3, 4, -4};
    src.translation_pool = {100, 200, 300, 400, 500, 600};

    AlaBoneTrack b1 = minimal_bone("Hip", 1);
    b1.idx_rotation = 0;
    b1.idx_translation = 0;
    b1.default_rotation = {1, 2, 3, 32767};
    b1.track_leaves.push_back(make_leaf(0x1007, {0x55, 0xAA}));
    src.bones.push_back(std::move(b1));

    AlaBoneTrack b2 = minimal_bone("Knee", 2);
    b2.idx_rotation = 1;
    src.bones.push_back(std::move(b2));

    auto bytes1 = tree_to_bytes(build_ala(src));
    AlaAnimation parsed = read_ala(bytes1.data(), bytes1.size());
    auto bytes2 = tree_to_bytes(build_ala(parsed));

    REQUIRE(bytes1 == bytes2);
}

TEST_CASE("read_ala captures raw payloads at every leaf for byte-identity") {
    AlaAnimation src = minimal_foc(5);
    src.bones.push_back(minimal_bone("B", 0));
    auto bytes = tree_to_bytes(build_ala(src));
    AlaAnimation parsed = read_ala(bytes.data(), bytes.size());

    REQUIRE_FALSE(parsed.raw_info_payload.empty());
    REQUIRE(parsed.bones.size() == 1);
    REQUIRE_FALSE(parsed.bones[0].raw_info_payload.empty());
}

TEST_CASE("read_ala detects FoC via mini-chunks 11/12/13 presence") {
    AlaAnimation foc = minimal_foc(2);
    foc.n_rotation_words = 0;  // FoC marker mini-chunks emitted even if zero
    auto bytes = tree_to_bytes(build_ala(foc));
    AlaAnimation parsed = read_ala(bytes.data(), bytes.size());
    REQUIRE(parsed.is_foc);
}

TEST_CASE("read_ala flags EaW (no FoC mini-chunks) as is_foc=false") {
    AlaAnimation eaw;
    eaw.is_foc = false;
    eaw.n_frames = 8;
    eaw.fps = 30.0f;
    auto bytes = tree_to_bytes(build_ala(eaw));
    AlaAnimation parsed = read_ala(bytes.data(), bytes.size());
    REQUIRE_FALSE(parsed.is_foc);
}

TEST_CASE("read_ala parses typed fields correctly from synthesised input") {
    AlaAnimation src = minimal_foc(42);
    src.n_rotation_words = 5;
    src.fps = 24.0f;
    AlaBoneTrack b = minimal_bone("Spine", 7);
    b.trans_offset[0] = 1.5f;
    b.trans_offset[1] = -2.5f;
    b.idx_rotation = 3;
    b.default_rotation = {100, -100, 200, -200};
    src.bones.push_back(b);

    auto bytes = tree_to_bytes(build_ala(src));
    AlaAnimation parsed = read_ala(bytes.data(), bytes.size());

    REQUIRE(parsed.n_frames == 42);
    REQUIRE(parsed.fps == 24.0f);
    REQUIRE(parsed.n_rotation_words == 5);
    REQUIRE(parsed.bones.size() == 1);
    REQUIRE(parsed.bones[0].name == "Spine");
    REQUIRE(parsed.bones[0].skeleton_index == 7);
    REQUIRE(parsed.bones[0].trans_offset[0] == 1.5f);
    REQUIRE(parsed.bones[0].trans_offset[1] == -2.5f);
    REQUIRE(parsed.bones[0].idx_rotation == 3);
    REQUIRE(parsed.bones[0].default_rotation[0] == 100);
    REQUIRE(parsed.bones[0].default_rotation[1] == -100);
}

TEST_CASE("read_ala parses pool int16 streams correctly (LE)") {
    AlaAnimation src = minimal_foc(2);
    src.n_rotation_words = 4;
    src.rotation_pool = {0x1234, static_cast<std::int16_t>(0xFFFF), 0x0000, 0x7FFF};
    auto bytes = tree_to_bytes(build_ala(src));
    AlaAnimation parsed = read_ala(bytes.data(), bytes.size());
    REQUIRE(parsed.rotation_pool.size() == 4);
    REQUIRE(parsed.rotation_pool[0] == 0x1234);
    REQUIRE(parsed.rotation_pool[1] == static_cast<std::int16_t>(0xFFFF));
    REQUIRE(parsed.rotation_pool[2] == 0x0000);
    REQUIRE(parsed.rotation_pool[3] == 0x7FFF);
}

TEST_CASE("Empty bones with both pools present round-trips") {
    AlaAnimation src = minimal_foc(1);
    src.n_rotation_words = 2;
    src.n_translation_words = 1;
    src.rotation_pool = {99, -99};
    src.translation_pool = {42};
    auto bytes1 = tree_to_bytes(build_ala(src));
    AlaAnimation parsed = read_ala(bytes1.data(), bytes1.size());
    auto bytes2 = tree_to_bytes(build_ala(parsed));
    REQUIRE(bytes1 == bytes2);
}

TEST_CASE("Missing 0x1000 root throws") {
    // A buffer with a totally different chunk at the top.
    std::vector<ChunkNode> tree;
    tree.push_back(make_leaf(0x9999, {0x00, 0x00, 0x00, 0x00}));
    auto bytes = tree_to_bytes(tree);
    REQUIRE_THROWS_AS(read_ala(bytes.data(), bytes.size()), std::runtime_error);
}

TEST_CASE("Truncated mini-chunk header in 0x1001 throws") {
    // Construct a manual 0x1000 -> 0x1001 with a truncated mini-chunk header.
    std::vector<ChunkNode> tree;
    std::vector<ChunkNode> root_kids;
    // Mini-chunk with id=1 but size byte missing — only the id is present.
    root_kids.push_back(make_leaf(0x1001, {1}));
    ChunkNode root;
    root.id = 0x1000;
    root.is_container = true;
    root.children = std::move(root_kids);
    tree.push_back(root);
    auto bytes = tree_to_bytes(tree);
    REQUIRE_THROWS_AS(read_ala(bytes.data(), bytes.size()), std::runtime_error);
}

TEST_CASE("Bone name with embedded zero terminator round-trips correctly") {
    AlaAnimation src = minimal_foc(1);
    src.bones.push_back(minimal_bone("Bone_With_Long_Name_42", 999));
    auto bytes = tree_to_bytes(build_ala(src));
    AlaAnimation parsed = read_ala(bytes.data(), bytes.size());
    REQUIRE(parsed.bones.size() == 1);
    REQUIRE(parsed.bones[0].name == "Bone_With_Long_Name_42");
    REQUIRE(parsed.bones[0].skeleton_index == 999);
}

TEST_CASE("Negative idx_rotation (-1 sentinel) round-trips") {
    AlaAnimation src = minimal_foc(1);
    AlaBoneTrack b = minimal_bone("B", 0);
    b.idx_rotation = -1;
    b.idx_translation = -1;
    b.idx_scale = -1;
    src.bones.push_back(b);
    auto bytes = tree_to_bytes(build_ala(src));
    AlaAnimation parsed = read_ala(bytes.data(), bytes.size());
    REQUIRE(parsed.bones[0].idx_rotation == -1);
    REQUIRE(parsed.bones[0].idx_translation == -1);
    REQUIRE(parsed.bones[0].idx_scale == -1);
}

TEST_CASE("Two consecutive read_ala calls on the same buffer are deterministic") {
    AlaAnimation src = minimal_foc(3);
    src.n_rotation_words = 2;
    src.rotation_pool = {1, 2};
    src.bones.push_back(minimal_bone("Hip", 0));
    auto bytes = tree_to_bytes(build_ala(src));

    AlaAnimation a1 = read_ala(bytes.data(), bytes.size());
    AlaAnimation a2 = read_ala(bytes.data(), bytes.size());
    auto b1 = tree_to_bytes(build_ala(a1));
    auto b2 = tree_to_bytes(build_ala(a2));
    REQUIRE(b1 == b2);
    REQUIRE(b1 == bytes);
}

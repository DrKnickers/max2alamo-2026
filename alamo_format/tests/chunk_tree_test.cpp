#include <catch2/catch_test_macros.hpp>

#include "alamo_format/chunk_tree.h"
#include "alamo_format/chunk_io.h"

#include <cstdint>
#include <vector>

using namespace alamo_format;

namespace {

// Build a tiny realistic-shaped .alo: skeleton with 0x201 (128 bytes,
// boneCount=2 + 124 zeros) and two 0x202 bones, each with name + data.
std::vector<std::uint8_t> build_minimal_alo() {
    ChunkWriter w;
    auto skel = w.begin_chunk(0x200, true);
    {
        auto info = w.begin_chunk(0x201, false);
        w.write_u32(2);
        w.write_zeros(124);
        w.end_chunk(info);

        for (const char* name : { "Root", "engines" }) {
            auto bone = w.begin_chunk(0x202, true);
            auto nm = w.begin_chunk(0x203, false);
            w.write_cstring(name);
            w.end_chunk(nm);

            auto bd = w.begin_chunk(0x206, false);
            w.write_u32(0xFFFFFFFFu);  // parent
            w.write_u32(1);            // visible
            w.write_u32(0);            // billboard
            for (int i = 0; i < 12; ++i) w.write_f32(static_cast<float>(i));
            w.end_chunk(bd);

            w.end_chunk(bone);
        }
    }
    w.end_chunk(skel);
    return w.release();
}

}  // namespace

TEST_CASE("Chunk tree round-trips byte-identically on minimal synthetic .alo") {
    auto original = build_minimal_alo();
    auto tree = read_chunk_tree(original.data(), original.size());
    auto rewritten = write_chunk_tree(tree);

    REQUIRE(rewritten.size() == original.size());
    REQUIRE(rewritten == original);
}

TEST_CASE("Chunk tree captures the right shape") {
    auto original = build_minimal_alo();
    auto tree = read_chunk_tree(original.data(), original.size());

    REQUIRE(tree.size() == 1);
    REQUIRE(tree[0].id == 0x200);
    REQUIRE(tree[0].is_container);
    REQUIRE(tree[0].children.size() == 3);  // 0x201 + two 0x202

    REQUIRE(tree[0].children[0].id == 0x201);
    REQUIRE_FALSE(tree[0].children[0].is_container);
    REQUIRE(tree[0].children[0].payload.size() == 128);

    REQUIRE(tree[0].children[1].id == 0x202);
    REQUIRE(tree[0].children[1].is_container);
    REQUIRE(tree[0].children[1].children.size() == 2);  // name + data

    REQUIRE(tree[0].children[1].children[0].id == 0x203);
    REQUIRE(tree[0].children[1].children[0].payload.size() == 5);  // "Root\0"

    REQUIRE(tree[0].children[1].children[1].id == 0x206);
    REQUIRE(tree[0].children[1].children[1].payload.size() == 60);
}

TEST_CASE("Empty input parses to an empty tree") {
    std::vector<std::uint8_t> empty;
    auto tree = read_chunk_tree(empty.data(), empty.size());
    REQUIRE(tree.empty());
    auto rewritten = write_chunk_tree(tree);
    REQUIRE(rewritten.empty());
}

TEST_CASE("Sibling chunks at top level round-trip") {
    // Simulate the .alo top-level structure: skeleton + mesh + connections,
    // each just an empty container.
    ChunkWriter w;
    for (std::uint32_t id : { 0x200u, 0x400u, 0x600u }) {
        auto h = w.begin_chunk(id, true);
        w.end_chunk(h);
    }
    auto original = w.release();

    auto tree = read_chunk_tree(original.data(), original.size());
    REQUIRE(tree.size() == 3);
    REQUIRE(tree[0].id == 0x200);
    REQUIRE(tree[1].id == 0x400);
    REQUIRE(tree[2].id == 0x600);

    auto rewritten = write_chunk_tree(tree);
    REQUIRE(rewritten == original);
}

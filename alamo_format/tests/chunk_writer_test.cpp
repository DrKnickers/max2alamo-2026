#include <catch2/catch_test_macros.hpp>

#include "alamo_format/chunk_io.h"

#include <cstring>
#include <vector>

using namespace alamo_format;

TEST_CASE("ChunkWriter emits a leaf chunk with the right header layout") {
    ChunkWriter w;
    auto h = w.begin_chunk(0x201, /*container*/ false);
    w.write_u32(7);
    w.end_chunk(h);

    const auto& buf = w.buffer();
    REQUIRE(buf.size() == 12);  // 8-byte header + 4-byte payload

    std::uint32_t id = 0, size_word = 0;
    std::memcpy(&id, buf.data() + 0, 4);
    std::memcpy(&size_word, buf.data() + 4, 4);
    REQUIRE(id == 0x201u);
    REQUIRE_FALSE(is_container_chunk(size_word));
    REQUIRE(decode_payload_size(size_word) == 4u);
}

TEST_CASE("ChunkWriter sets the container flag for containers") {
    ChunkWriter w;
    auto outer = w.begin_chunk(0x200, /*container*/ true);
    auto inner = w.begin_chunk(0x201, /*container*/ false);
    w.write_u32(0);
    w.end_chunk(inner);
    w.end_chunk(outer);

    const auto& buf = w.buffer();
    std::uint32_t outer_size_word = 0;
    std::memcpy(&outer_size_word, buf.data() + 4, 4);
    REQUIRE(is_container_chunk(outer_size_word));
    REQUIRE(decode_payload_size(outer_size_word) == 12u);  // inner = 8 + 4

    std::uint32_t inner_size_word = 0;
    std::memcpy(&inner_size_word, buf.data() + 8 + 4, 4);
    REQUIRE_FALSE(is_container_chunk(inner_size_word));
    REQUIRE(decode_payload_size(inner_size_word) == 4u);
}

TEST_CASE("ChunkWriter mini-chunks pack their size into one byte") {
    ChunkWriter w;
    auto h = w.begin_chunk(0x603, /*container*/ false);
    {
        auto m1 = w.begin_mini(5);  // proxy name
        w.write_cstring("HP_Test");
        w.end_mini(m1);
        auto m2 = w.begin_mini(6);  // bone index
        w.write_u32(1);
        w.end_mini(m2);
    }
    w.end_chunk(h);

    const auto& buf = w.buffer();
    // Top header (8) + mini-1 header (2) + "HP_Test\0" (8) + mini-2 header (2) + u32 (4) = 24
    REQUIRE(buf.size() == 24);

    REQUIRE(buf[8]  == 5);   // mini id
    REQUIRE(buf[9]  == 8);   // payload size (string + null)
    REQUIRE(buf[18] == 6);
    REQUIRE(buf[19] == 4);
}

TEST_CASE("ChunkWriter -> ChunkReader round-trips a synthetic file") {
    ChunkWriter w;
    auto outer = w.begin_chunk(0x200, true);
    {
        auto info = w.begin_chunk(0x201, false);
        w.write_u32(1);
        w.write_zeros(124);
        w.end_chunk(info);

        auto bone = w.begin_chunk(0x202, true);
        {
            auto name = w.begin_chunk(0x203, false);
            w.write_cstring("Root");
            w.end_chunk(name);
        }
        w.end_chunk(bone);
    }
    w.end_chunk(outer);

    auto bytes = w.release();
    ChunkReader r(bytes.data(), bytes.size());
    auto h = r.read_header();
    REQUIRE(h.id == 0x200);
    REQUIRE(h.is_container);

    ChunkReader sub = r.subreader(h);
    auto info = sub.read_header();
    REQUIRE(info.id == 0x201);
    REQUIRE(info.payload_size == 128);
    {
        ChunkReader ir = sub.subreader(info);
        REQUIRE(ir.read_u32() == 1u);
    }
    sub.skip_payload(info);

    auto bone = sub.read_header();
    REQUIRE(bone.id == 0x202);
    REQUIRE(bone.is_container);
    {
        ChunkReader br = sub.subreader(bone);
        auto name = br.read_header();
        REQUIRE(name.id == 0x203);
        ChunkReader nr = br.subreader(name);
        REQUIRE(nr.read_cstring() == "Root");
    }
    sub.skip_payload(bone);

    REQUIRE(sub.eof());
}

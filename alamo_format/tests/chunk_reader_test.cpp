#include <catch2/catch_test_macros.hpp>

#include "alamo_format/chunk_io.h"

#include <cstdint>
#include <vector>

using namespace alamo_format;

namespace {

// Build a tiny synthetic .alo-shaped buffer:
//   container 0x200 (skeleton), payload = leaf 0x201 with [u32 boneCount=2]
//                              + container 0x202 (bone), payload = leaf 0x203 ("hi")
//                              + container 0x202 (bone), payload = leaf 0x203 ("there")
std::vector<std::uint8_t> build_synthetic() {
    ChunkWriter w;
    auto skel = w.begin_chunk(0x200, /*container*/ true);
    {
        auto info = w.begin_chunk(0x201, /*container*/ false);
        w.write_u32(2);
        w.write_zeros(124);
        w.end_chunk(info);

        auto b1 = w.begin_chunk(0x202, /*container*/ true);
        {
            auto n1 = w.begin_chunk(0x203, false);
            w.write_cstring("hi");
            w.end_chunk(n1);
        }
        w.end_chunk(b1);

        auto b2 = w.begin_chunk(0x202, /*container*/ true);
        {
            auto n2 = w.begin_chunk(0x203, false);
            w.write_cstring("there");
            w.end_chunk(n2);
        }
        w.end_chunk(b2);
    }
    w.end_chunk(skel);
    return w.release();
}

}  // namespace

TEST_CASE("ChunkReader walks the synthetic skeleton structure") {
    auto buf = build_synthetic();
    ChunkReader top(buf.data(), buf.size());

    auto skel = top.read_header();
    REQUIRE(skel.id == 0x200);
    REQUIRE(skel.is_container);
    REQUIRE(skel.payload_size > 0);

    ChunkReader sk = top.subreader(skel);
    auto info = sk.read_header();
    REQUIRE(info.id == 0x201);
    REQUIRE_FALSE(info.is_container);
    REQUIRE(info.payload_size == 128);

    // Bone count is the first u32 of 0x201.
    {
        ChunkReader infoR = sk.subreader(info);
        REQUIRE(infoR.read_u32() == 2u);
    }
    sk.skip_payload(info);

    // First bone container.
    auto b1 = sk.read_header();
    REQUIRE(b1.id == 0x202);
    REQUIRE(b1.is_container);
    {
        ChunkReader br = sk.subreader(b1);
        auto name = br.read_header();
        REQUIRE(name.id == 0x203);
        REQUIRE_FALSE(name.is_container);
        ChunkReader nr = br.subreader(name);
        REQUIRE(nr.read_cstring() == "hi");
    }
    sk.skip_payload(b1);

    auto b2 = sk.read_header();
    REQUIRE(b2.id == 0x202);
    {
        ChunkReader br = sk.subreader(b2);
        auto name = br.read_header();
        ChunkReader nr = br.subreader(name);
        REQUIRE(nr.read_cstring() == "there");
    }
    sk.skip_payload(b2);

    REQUIRE(sk.eof());
    top.skip_payload(skel);
    REQUIRE(top.eof());
}

TEST_CASE("ChunkReader rejects truncated headers") {
    std::vector<std::uint8_t> buf{ 0x00, 0x02, 0x00, 0x00 };  // only 4 bytes, header is 8
    ChunkReader r(buf.data(), buf.size());
    REQUIRE_THROWS_AS(r.read_header(), std::runtime_error);
}

TEST_CASE("ChunkReader rejects chunks that overflow the buffer") {
    // Header claims 100 bytes payload but the buffer only has 8 + 2.
    std::vector<std::uint8_t> buf{
        0x00, 0x02, 0x00, 0x00,  // id 0x200
        0x64, 0x00, 0x00, 0x00,  // size = 100, container flag clear
        0xAA, 0xBB
    };
    ChunkReader r(buf.data(), buf.size());
    REQUIRE_THROWS_AS(r.read_header(), std::runtime_error);
}

TEST_CASE("ChunkReader overflow error message renders the chunk ID in hex") {
    // The same scenario as above; verify the id appears as 0x200 (not decimal 512).
    std::vector<std::uint8_t> buf{
        0x00, 0x02, 0x00, 0x00,  // id 0x200
        0xFF, 0x00, 0x00, 0x00,  // size = 255
    };
    ChunkReader r(buf.data(), buf.size());
    try {
        r.read_header();
        FAIL("expected throw");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("0x200") != std::string::npos);  // hex, not decimal
        REQUIRE(msg.find("0x512") == std::string::npos);  // would be the decimal-with-0x bug
    }
}

TEST_CASE("ChunkReader read_cstring stops at the null terminator") {
    std::vector<std::uint8_t> buf{ 'h', 'i', 0, 'X', 'Y' };
    ChunkReader r(buf.data(), buf.size());
    REQUIRE(r.read_cstring() == "hi");
    REQUIRE(r.cursor() == 3);  // past the null
    REQUIRE(r.read_u8() == 'X');
    REQUIRE(r.read_u8() == 'Y');
    REQUIRE(r.eof());
}

TEST_CASE("ChunkReader primitive types are little-endian") {
    std::vector<std::uint8_t> buf{
        0x78, 0x56, 0x34, 0x12,  // u32: 0x12345678
        0xCD, 0xAB,              // u16: 0xABCD
        0xFF                      // u8
    };
    ChunkReader r(buf.data(), buf.size());
    REQUIRE(r.read_u32() == 0x12345678u);
    REQUIRE(r.read_u16() == 0xABCDu);
    REQUIRE(r.read_u8()  == 0xFFu);
}

#include <catch2/catch_test_macros.hpp>

#include "alamo_format/chunk_io.h"

using namespace alamo_format;

TEST_CASE("encode_chunk_size sets the high bit for containers") {
    REQUIRE(encode_chunk_size(0, true)  == 0x80000000u);
    REQUIRE(encode_chunk_size(0, false) == 0x00000000u);
    REQUIRE(encode_chunk_size(0x13C, true)  == 0x8000013Cu);
    REQUIRE(encode_chunk_size(0x13C, false) == 0x0000013Cu);
}

TEST_CASE("decode_payload_size strips the container flag") {
    REQUIRE(decode_payload_size(0x8000013C) == 0x13Cu);
    REQUIRE(decode_payload_size(0x0000013C) == 0x13Cu);
    REQUIRE(decode_payload_size(0x80000000) == 0u);
    REQUIRE(decode_payload_size(0x7FFFFFFF) == 0x7FFFFFFFu);
}

TEST_CASE("is_container_chunk reads the high bit") {
    REQUIRE(is_container_chunk(0x80000000));
    REQUIRE(is_container_chunk(0x8000013C));
    REQUIRE_FALSE(is_container_chunk(0x00000000));
    REQUIRE_FALSE(is_container_chunk(0x7FFFFFFF));
}

TEST_CASE("encode/decode is a round trip") {
    for (std::uint32_t size : { 0u, 1u, 7u, 8u, 0x7Fu, 0x80u, 0xFFFFu, 0x100000u, 0x7FFFFFFFu }) {
        for (bool container : { false, true }) {
            std::uint32_t enc = encode_chunk_size(size, container);
            REQUIRE(decode_payload_size(enc) == size);
            REQUIRE(is_container_chunk(enc) == container);
        }
    }
}

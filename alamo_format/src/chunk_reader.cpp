#include "alamo_format/chunk_io.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace alamo_format {

namespace {

void read_le_bytes(const std::uint8_t* src, void* dst, std::size_t n) noexcept {
    // x86 / x86_64 are little-endian and unaligned-safe; just memcpy.
    std::memcpy(dst, src, n);
}

}  // namespace

void ChunkReader::require(std::size_t n) {
    if (remaining() < n) {
        throw std::runtime_error("ChunkReader: short read at offset "
            + std::to_string(cursor_) + ", needed " + std::to_string(n)
            + ", available " + std::to_string(remaining()));
    }
}

ChunkHeader ChunkReader::read_header() {
    require(kChunkHeaderSize);
    std::uint32_t id = 0, size_word = 0;
    read_le_bytes(data_ + cursor_, &id, 4);
    read_le_bytes(data_ + cursor_ + 4, &size_word, 4);
    ChunkHeader h{
        /*id*/            id,
        /*payload_size*/  decode_payload_size(size_word),
        /*is_container*/  is_container_chunk(size_word),
        /*header_offset*/ cursor_,
        /*payload_offset*/ cursor_ + kChunkHeaderSize,
    };
    cursor_ += kChunkHeaderSize;
    if (cursor_ + h.payload_size > end_) {
        throw std::runtime_error("ChunkReader: chunk payload exceeds buffer at offset "
            + std::to_string(h.header_offset) + " (id=0x"
            + std::to_string(h.id) + ", size=" + std::to_string(h.payload_size) + ")");
    }
    return h;
}

MiniChunkHeader ChunkReader::read_mini_header() {
    require(kMiniHeaderSize);
    MiniChunkHeader h{
        data_[cursor_],
        data_[cursor_ + 1],
        cursor_,
        cursor_ + kMiniHeaderSize,
    };
    cursor_ += kMiniHeaderSize;
    if (cursor_ + h.payload_size > end_) {
        throw std::runtime_error("ChunkReader: mini-chunk payload exceeds buffer at offset "
            + std::to_string(h.header_offset));
    }
    return h;
}

std::uint8_t ChunkReader::read_u8() {
    require(1);
    return data_[cursor_++];
}

std::uint16_t ChunkReader::read_u16() {
    require(2);
    std::uint16_t v = 0;
    read_le_bytes(data_ + cursor_, &v, 2);
    cursor_ += 2;
    return v;
}

std::uint32_t ChunkReader::read_u32() {
    require(4);
    std::uint32_t v = 0;
    read_le_bytes(data_ + cursor_, &v, 4);
    cursor_ += 4;
    return v;
}

std::int16_t ChunkReader::read_i16() {
    require(2);
    std::int16_t v = 0;
    read_le_bytes(data_ + cursor_, &v, 2);
    cursor_ += 2;
    return v;
}

std::int32_t ChunkReader::read_i32() {
    require(4);
    std::int32_t v = 0;
    read_le_bytes(data_ + cursor_, &v, 4);
    cursor_ += 4;
    return v;
}

float ChunkReader::read_f32() {
    require(4);
    float v = 0.0f;
    read_le_bytes(data_ + cursor_, &v, 4);
    cursor_ += 4;
    return v;
}

std::vector<std::uint8_t> ChunkReader::read_bytes(std::size_t n) {
    require(n);
    std::vector<std::uint8_t> out(data_ + cursor_, data_ + cursor_ + n);
    cursor_ += n;
    return out;
}

std::string ChunkReader::read_cstring() {
    std::size_t start = cursor_;
    while (cursor_ < end_ && data_[cursor_] != 0) ++cursor_;
    if (cursor_ >= end_) {
        throw std::runtime_error("ChunkReader: unterminated string starting at offset "
            + std::to_string(start));
    }
    std::string s(reinterpret_cast<const char*>(data_ + start), cursor_ - start);
    ++cursor_;  // skip null terminator
    return s;
}

void ChunkReader::skip(std::size_t n) {
    require(n);
    cursor_ += n;
}

}  // namespace alamo_format

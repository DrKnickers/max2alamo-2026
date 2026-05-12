#include "alamo_format/chunk_io.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace alamo_format {

void ChunkWriter::patch_u32(std::size_t offset, std::uint32_t v) noexcept {
    std::memcpy(buf_.data() + offset, &v, 4);
}

void ChunkWriter::patch_u8(std::size_t offset, std::uint8_t v) noexcept {
    buf_[offset] = v;
}

ChunkWriter::ChunkHandle ChunkWriter::begin_chunk(std::uint32_t id, bool is_container) {
    ChunkHandle h{ buf_.size(), id, is_container };
    // Reserve 8 bytes for the header; payload size patched in end_chunk().
    write_u32(id);
    write_u32(0);  // placeholder
    return h;
}

void ChunkWriter::end_chunk(const ChunkHandle& h) {
    const std::size_t payload_size = buf_.size() - (h.header_offset + kChunkHeaderSize);
    if (payload_size > kSizeMask) {
        throw std::runtime_error("ChunkWriter: chunk payload exceeds 2GB limit");
    }
    patch_u32(h.header_offset + 4,
              encode_chunk_size(static_cast<std::uint32_t>(payload_size), h.is_container));
}

ChunkWriter::MiniHandle ChunkWriter::begin_mini(std::uint8_t id) {
    MiniHandle h{ buf_.size(), id };
    write_u8(id);
    write_u8(0);  // placeholder
    return h;
}

void ChunkWriter::end_mini(const MiniHandle& h) {
    const std::size_t payload_size = buf_.size() - (h.header_offset + kMiniHeaderSize);
    if (payload_size > 255) {
        throw std::runtime_error("ChunkWriter: mini-chunk payload exceeds 255 bytes");
    }
    patch_u8(h.header_offset + 1, static_cast<std::uint8_t>(payload_size));
}

void ChunkWriter::write_u8(std::uint8_t v) {
    buf_.push_back(v);
}

void ChunkWriter::write_u16(std::uint16_t v) {
    const std::size_t off = buf_.size();
    buf_.resize(off + 2);
    std::memcpy(buf_.data() + off, &v, 2);
}

void ChunkWriter::write_u32(std::uint32_t v) {
    const std::size_t off = buf_.size();
    buf_.resize(off + 4);
    std::memcpy(buf_.data() + off, &v, 4);
}

void ChunkWriter::write_i16(std::int16_t v) {
    const std::size_t off = buf_.size();
    buf_.resize(off + 2);
    std::memcpy(buf_.data() + off, &v, 2);
}

void ChunkWriter::write_i32(std::int32_t v) {
    const std::size_t off = buf_.size();
    buf_.resize(off + 4);
    std::memcpy(buf_.data() + off, &v, 4);
}

void ChunkWriter::write_f32(float v) {
    const std::size_t off = buf_.size();
    buf_.resize(off + 4);
    std::memcpy(buf_.data() + off, &v, 4);
}

void ChunkWriter::write_bytes(const std::uint8_t* p, std::size_t n) {
    buf_.insert(buf_.end(), p, p + n);
}

void ChunkWriter::write_zeros(std::size_t n) {
    buf_.insert(buf_.end(), n, std::uint8_t{0});
}

void ChunkWriter::write_cstring(const std::string& s) {
    buf_.insert(buf_.end(), s.begin(), s.end());
    buf_.push_back(0);
}

}  // namespace alamo_format

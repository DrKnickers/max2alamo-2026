#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace alamo_format {

// Petroglyph's chunked-file framing.
//
// Layout: each chunk is `[uint32 id][uint32 size_with_flag][payload]`,
// little-endian. Sizes count payload bytes only; the 8-byte header is not
// included. The high bit of the size word distinguishes a CONTAINER chunk
// (whose payload is itself a sequence of further chunks) from a LEAF chunk
// (whose payload is data). This was confirmed by direct observation of
// vanilla EaW .alo files and by Mike Lankamp's importer (alamo2max.ms:251).
//
//   size_with_flag = (payload_size & 0x7FFFFFFF) | (is_container ? 0x80000000 : 0)
//
// Inside leaf chunks, MINI-CHUNKS may also appear with a smaller header:
//   [uint8 id][uint8 size][payload of `size` bytes]
// Used for the variable-length parameter blocks in 0x601 connections,
// 0x603 proxies, 0x10102-0x10106 shader params, and 0x1003 / 0x1001 anim
// info chunks.

constexpr std::uint32_t kSizeMask        = 0x7FFFFFFFu;
constexpr std::uint32_t kContainerFlag   = 0x80000000u;
constexpr std::size_t   kChunkHeaderSize = 8;
constexpr std::size_t   kMiniHeaderSize  = 2;

inline std::uint32_t encode_chunk_size(std::uint32_t payload_size, bool is_container) noexcept {
    return (payload_size & kSizeMask) | (is_container ? kContainerFlag : 0u);
}

inline std::uint32_t decode_payload_size(std::uint32_t encoded) noexcept {
    return encoded & kSizeMask;
}

inline bool is_container_chunk(std::uint32_t encoded) noexcept {
    return (encoded & kContainerFlag) != 0u;
}

// ---- Reader ----------------------------------------------------------------

struct ChunkHeader {
    std::uint32_t id;
    std::uint32_t payload_size;
    bool          is_container;
    std::size_t   header_offset;   // file offset of the 8-byte header
    std::size_t   payload_offset;  // header_offset + 8
};

struct MiniChunkHeader {
    std::uint8_t  id;
    std::uint8_t  payload_size;
    std::size_t   header_offset;
    std::size_t   payload_offset;
};

// Non-owning view over a buffer. `offset` is the read cursor; `end` is the
// exclusive upper bound for the current scope (top-level file initially,
// or a container's payload window when descending).
class ChunkReader {
public:
    ChunkReader(const std::uint8_t* data, std::size_t size) noexcept
        : data_(data), end_(size), cursor_(0) {}

    bool eof() const noexcept { return cursor_ >= end_; }
    std::size_t cursor() const noexcept { return cursor_; }
    std::size_t end() const noexcept { return end_; }
    std::size_t remaining() const noexcept { return end_ > cursor_ ? end_ - cursor_ : 0; }

    // Read one chunk header at the current cursor; advances past the 8-byte
    // header so the cursor sits at the start of the payload. Throws on EOF
    // or truncated header.
    ChunkHeader read_header();

    // Read a mini-chunk header (2 bytes). Throws on EOF.
    MiniChunkHeader read_mini_header();

    // Skip the current chunk's payload (call after read_header to advance to
    // the next sibling chunk). Pass the header you just read.
    void skip_payload(const ChunkHeader& h) noexcept { cursor_ = h.payload_offset + h.payload_size; }
    void skip_mini_payload(const MiniChunkHeader& h) noexcept { cursor_ = h.payload_offset + h.payload_size; }

    // Spawn a sub-reader limited to a container's payload window. Useful for
    // descending without confusing the parent cursor.
    ChunkReader subreader(const ChunkHeader& h) const noexcept {
        return ChunkReader(data_ + h.payload_offset, h.payload_size);
    }
    ChunkReader subreader(const MiniChunkHeader& h) const noexcept {
        return ChunkReader(data_ + h.payload_offset, h.payload_size);
    }

    // Primitive readers — read from the current cursor and advance it. Throw
    // on EOF.
    std::uint8_t  read_u8();
    std::uint16_t read_u16();
    std::uint32_t read_u32();
    std::int16_t  read_i16();
    std::int32_t  read_i32();
    float         read_f32();

    // Read N raw bytes starting at the cursor (advances).
    std::vector<std::uint8_t> read_bytes(std::size_t n);

    // Read a null-terminated ASCII string. Advances past the terminator. The
    // returned string excludes the null. Throws if no terminator is found
    // before `end_`.
    std::string read_cstring();

    // Skip `n` bytes. Throws on EOF.
    void skip(std::size_t n);

    // Raw access for callers that need to peek without advancing.
    const std::uint8_t* data() const noexcept { return data_; }

private:
    const std::uint8_t* data_;
    std::size_t end_;
    std::size_t cursor_;

    void require(std::size_t n);
};

// ---- Writer ----------------------------------------------------------------

// Append-only writer that buffers bytes and tracks chunk-header back-patches
// so containers can be written without knowing their payload size up-front.
class ChunkWriter {
public:
    // Begin a chunk; reserves space for the 8-byte header. Returns an opaque
    // handle that must be passed back to end_chunk(). Chunks may nest.
    struct ChunkHandle {
        std::size_t header_offset;
        std::uint32_t id;
        bool is_container;
    };

    ChunkHandle begin_chunk(std::uint32_t id, bool is_container);
    void end_chunk(const ChunkHandle& h);

    // Mini-chunks (inside a leaf chunk's payload). Header is 2 bytes; size is
    // patched on end_mini_chunk().
    struct MiniHandle {
        std::size_t header_offset;
        std::uint8_t id;
    };
    MiniHandle begin_mini(std::uint8_t id);
    void end_mini(const MiniHandle& h);

    // Primitive writers — append at end. Little-endian.
    void write_u8(std::uint8_t v);
    void write_u16(std::uint16_t v);
    void write_u32(std::uint32_t v);
    void write_i16(std::int16_t v);
    void write_i32(std::int32_t v);
    void write_f32(float v);
    void write_bytes(const std::uint8_t* p, std::size_t n);
    void write_zeros(std::size_t n);
    // Writes the string plus a null terminator.
    void write_cstring(const std::string& s);

    const std::vector<std::uint8_t>& buffer() const noexcept { return buf_; }
    std::vector<std::uint8_t> release() noexcept { return std::move(buf_); }
    std::size_t size() const noexcept { return buf_.size(); }

private:
    std::vector<std::uint8_t> buf_;

    void patch_u32(std::size_t offset, std::uint32_t v) noexcept;
    void patch_u8(std::size_t offset, std::uint8_t v) noexcept;
};

}  // namespace alamo_format

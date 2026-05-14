#include "alamo_format/legacy_clip_scan.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace alamo_format {

namespace {

// AlamoUtility ClassDesc Class_ID, recovered Phase 11a via MSVC RTTI
// walk of legacy max2alamo.dle. Little-endian 8-byte signature as it
// appears verbatim inside legacy .max files' appData records:
//   Class_ID(0x70a24090, 0x60c90f03)
// = 90 40 a2 70 03 0f c9 60
constexpr unsigned char kLegacyUtilityClassIdBytes[8] = {
    0x90, 0x40, 0xa2, 0x70, 0x03, 0x0f, 0xc9, 0x60,
};

constexpr std::size_t kRecordStride  = 342;
constexpr std::size_t kNameOffset    = 34;
constexpr std::size_t kNameMaxLen    = 16;
constexpr std::size_t kStartOffset   = 290;  // uint16 LE
constexpr std::size_t kEndOffset     = 294;  // uint16 LE
constexpr std::size_t kIndexOffset   = 12;   // uint32 LE

std::uint16_t read_u16_le(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t read_u32_le(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8)  |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

// Returns the offset of the next Class_ID match at or after `from`,
// or `size` if no further match. memcmp-based; runs comfortably on
// 20MB fixtures in well under a second.
std::size_t find_next_class_id(const unsigned char* data,
                               std::size_t size,
                               std::size_t from)
{
    if (size < 8) return size;
    const std::size_t last = size - 8;
    for (std::size_t i = from; i <= last; ++i) {
        if (data[i] == kLegacyUtilityClassIdBytes[0] &&
            std::memcmp(data + i, kLegacyUtilityClassIdBytes, 8) == 0) {
            return i;
        }
    }
    return size;
}

}  // namespace

std::vector<LegacyClipRecord>
scan_legacy_clip_records(const unsigned char* data, std::size_t size)
{
    std::vector<LegacyClipRecord> out;
    if (data == nullptr || size < kRecordStride) return out;

    std::size_t pos = 0;
    while (pos + kRecordStride <= size) {
        const std::size_t hit = find_next_class_id(data, size, pos);
        if (hit + kRecordStride > size) break;  // truncated trailing match

        const unsigned char* rec = data + hit;

        // Name: ASCII bytes at +34, null-terminator within 16 bytes
        // (or hard-cap at 16). Strip the terminator and anything after.
        std::string name;
        name.reserve(kNameMaxLen);
        for (std::size_t i = 0; i < kNameMaxLen; ++i) {
            const unsigned char ch = rec[kNameOffset + i];
            if (ch == 0) break;
            name.push_back(static_cast<char>(ch));
        }

        const std::uint32_t idx   = read_u32_le(rec + kIndexOffset);
        const std::uint16_t start = read_u16_le(rec + kStartOffset);
        const std::uint16_t end   = read_u16_le(rec + kEndOffset);

        // Drop malformed records rather than fall over: empty name or
        // inverted range. The legacy plugin's authoring UI prevented
        // these in practice, but harden the reader anyway.
        const bool well_formed =
            !name.empty() &&
            static_cast<int>(end) >= static_cast<int>(start);

        if (well_formed) {
            LegacyClipRecord r;
            r.index       = static_cast<int>(idx);
            r.name        = std::move(name);
            r.start_frame = static_cast<int>(start);
            r.end_frame   = static_cast<int>(end);
            out.push_back(std::move(r));
        }

        pos = hit + kRecordStride;  // step exactly one stride so we
                                    // don't double-count inside a
                                    // record body that incidentally
                                    // contains the Class_ID bytes
                                    // (none of the fixtures do, but
                                    // be deterministic).
    }
    return out;
}

}  // namespace alamo_format

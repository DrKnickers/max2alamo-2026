// Catch2 tests for the Phase 11c legacy clip scanner. Pure-C++ logic;
// the scanner operates on a raw byte buffer so unit tests don't need
// any Max SDK / fixture-file access. Synthetic byte buffers built
// in-test mimic the legacy plugin's 342-byte record format pinned by
// 11a/11c byte-differential against CIS_SBD / EI_SNOWTROOPER / Stormtrooper.

#include <catch2/catch_test_macros.hpp>

#include "alamo_format/legacy_clip_scan.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using alamo_format::LegacyClipRecord;
using alamo_format::scan_legacy_clip_records;

namespace {

// Bytes verbatim per Phase 11a recovery: Class_ID(0x70a24090, 0x60c90f03).
constexpr std::array<unsigned char, 8> kClassIdBytes = {
    0x90, 0x40, 0xa2, 0x70, 0x03, 0x0f, 0xc9, 0x60,
};

constexpr std::size_t kRecordStride = 342;
constexpr std::size_t kNameOffset   = 34;
constexpr std::size_t kStartOffset  = 290;
constexpr std::size_t kEndOffset    = 294;
constexpr std::size_t kIndexOffset  = 12;

// Build one synthetic 342-byte record with given fields. Other bytes
// are left zero -- the scanner ignores everything outside the four
// known fields, so zero-padding is a faithful and minimal fixture
// (mirrors what we observed in CIS_SBD.max where the record body is
// almost entirely zero outside name + start + end).
std::vector<unsigned char> make_record(std::uint32_t index,
                                       const std::string& name,
                                       std::uint16_t start,
                                       std::uint16_t end)
{
    std::vector<unsigned char> r(kRecordStride, 0u);
    std::memcpy(r.data(), kClassIdBytes.data(), kClassIdBytes.size());
    // +12 uint32 LE index
    r[kIndexOffset + 0] = static_cast<unsigned char>(index & 0xffu);
    r[kIndexOffset + 1] = static_cast<unsigned char>((index >> 8) & 0xffu);
    r[kIndexOffset + 2] = static_cast<unsigned char>((index >> 16) & 0xffu);
    r[kIndexOffset + 3] = static_cast<unsigned char>((index >> 24) & 0xffu);
    // +34 ASCII name, null-terminated (16-byte field, caller's
    // responsibility to keep name length <= 15 chars + NUL).
    std::memcpy(r.data() + kNameOffset, name.data(), name.size());
    // +290 / +294 uint16 LE
    r[kStartOffset + 0] = static_cast<unsigned char>(start & 0xffu);
    r[kStartOffset + 1] = static_cast<unsigned char>((start >> 8) & 0xffu);
    r[kEndOffset + 0]   = static_cast<unsigned char>(end & 0xffu);
    r[kEndOffset + 1]   = static_cast<unsigned char>((end >> 8) & 0xffu);
    return r;
}

// Concatenate per-record byte vectors into one buffer suitable for
// scan_legacy_clip_records. Mimics how the records appear in a real
// .max file (back-to-back, no inter-record padding).
std::vector<unsigned char>
concat(const std::vector<std::vector<unsigned char>>& parts,
       std::size_t leading_padding = 0)
{
    std::vector<unsigned char> out(leading_padding, 0u);
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

}  // namespace

// =========================================================================
// Empty / pathological inputs
// =========================================================================

TEST_CASE("scan_legacy_clip_records: nullptr buffer yields empty list") {
    auto recs = scan_legacy_clip_records(nullptr, 0);
    REQUIRE(recs.empty());
}

TEST_CASE("scan_legacy_clip_records: tiny buffer (< 1 record) yields empty list") {
    std::vector<unsigned char> tiny(100, 0xffu);
    auto recs = scan_legacy_clip_records(tiny.data(), tiny.size());
    REQUIRE(recs.empty());
}

TEST_CASE("scan_legacy_clip_records: buffer with no Class_ID pattern yields empty list") {
    std::vector<unsigned char> buf(4096, 0xa5u);  // arbitrary non-matching fill
    auto recs = scan_legacy_clip_records(buf.data(), buf.size());
    REQUIRE(recs.empty());
}

// =========================================================================
// Single-record decoding
// =========================================================================

TEST_CASE("scan_legacy_clip_records: decodes a single well-formed record") {
    auto rec = make_record(0, "MOVE_00", 1, 30);
    auto recs = scan_legacy_clip_records(rec.data(), rec.size());
    REQUIRE(recs.size() == 1);
    REQUIRE(recs[0].index == 0);
    REQUIRE(recs[0].name == "MOVE_00");
    REQUIRE(recs[0].start_frame == 1);
    REQUIRE(recs[0].end_frame == 30);
}

TEST_CASE("scan_legacy_clip_records: record at non-zero offset still decodes") {
    // Real .max files have ~1MB of OLE compound storage / scene-stream
    // preamble before the first clip record. Simulate with leading padding.
    auto rec = make_record(0, "IDLE_00", 31, 81);
    auto buf = concat({rec}, /*leading_padding=*/1024);
    auto recs = scan_legacy_clip_records(buf.data(), buf.size());
    REQUIRE(recs.size() == 1);
    REQUIRE(recs[0].name == "IDLE_00");
    REQUIRE(recs[0].start_frame == 31);
    REQUIRE(recs[0].end_frame == 81);
}

// =========================================================================
// Multi-record: CIS_SBD-style fixture (matches the real .max bytes)
// =========================================================================

TEST_CASE("scan_legacy_clip_records: CIS_SBD-style 8 clips, contiguous timeline") {
    // Exact (name, start, end) tuples decoded from
    // tests/corpus/legacy/ThrREv/Ascendancy/Super Battle Droid/CIS_SBD.max
    // by re/scripts/extract_legacy_clip_ranges.py (Phase 11c research).
    // Sequential timeline: each start == prev_end + 1.
    auto buf = concat({
        make_record(0, "MOVE_00",        1,  30),
        make_record(1, "IDLE_00",       31,  81),
        make_record(2, "IDLE_01",       82, 180),
        make_record(3, "TRANSITION_00",181, 200),
        make_record(4, "ATTACK_00",    201, 220),
        make_record(5, "ATTACKIDLE_00",221, 280),
        make_record(6, "DIE_00",       281, 330),
        make_record(7, "DIE_01",       331, 400),
    });
    auto recs = scan_legacy_clip_records(buf.data(), buf.size());
    REQUIRE(recs.size() == 8);
    REQUIRE(recs[0].name == "MOVE_00");
    REQUIRE(recs[0].start_frame == 1);
    REQUIRE(recs[0].end_frame == 30);
    REQUIRE(recs[7].name == "DIE_01");
    REQUIRE(recs[7].start_frame == 331);
    REQUIRE(recs[7].end_frame == 400);
    // Contiguous-timeline invariant the legacy plugin authored:
    for (std::size_t i = 1; i < recs.size(); ++i) {
        REQUIRE(recs[i].start_frame == recs[i - 1].end_frame + 1);
    }
}

TEST_CASE("scan_legacy_clip_records: indices preserved in discovery order") {
    auto buf = concat({
        make_record(7, "Z", 1, 2),  // out-of-order index field intentional
        make_record(3, "A", 3, 4),
    });
    auto recs = scan_legacy_clip_records(buf.data(), buf.size());
    REQUIRE(recs.size() == 2);
    // Discovery order == file order, not numeric index order.
    REQUIRE(recs[0].name == "Z");
    REQUIRE(recs[0].index == 7);
    REQUIRE(recs[1].name == "A");
    REQUIRE(recs[1].index == 3);
}

// =========================================================================
// Malformed records get dropped silently (don't crash, don't poison list)
// =========================================================================

TEST_CASE("scan_legacy_clip_records: empty-name record dropped") {
    auto rec = make_record(0, "", 1, 10);
    auto recs = scan_legacy_clip_records(rec.data(), rec.size());
    REQUIRE(recs.empty());
}

TEST_CASE("scan_legacy_clip_records: inverted range (end < start) dropped") {
    auto rec = make_record(0, "BAD", 100, 50);
    auto recs = scan_legacy_clip_records(rec.data(), rec.size());
    REQUIRE(recs.empty());
}

TEST_CASE("scan_legacy_clip_records: end == start accepted (1-frame clip)") {
    // Phase 8f confirmed our walker handles single-frame clips; we must
    // not reject one here just because end == start.
    auto rec = make_record(0, "POSE", 5, 5);
    auto recs = scan_legacy_clip_records(rec.data(), rec.size());
    REQUIRE(recs.size() == 1);
    REQUIRE(recs[0].start_frame == 5);
    REQUIRE(recs[0].end_frame == 5);
}

TEST_CASE("scan_legacy_clip_records: trailing partial record dropped, leading complete record kept") {
    auto rec = make_record(0, "MOVE_00", 1, 30);
    // Truncate the second record so it overlaps the buffer end.
    std::vector<unsigned char> buf = rec;
    auto rec2 = make_record(1, "PARTIAL", 31, 60);
    buf.insert(buf.end(), rec2.begin(), rec2.begin() + 100);  // half a record
    auto recs = scan_legacy_clip_records(buf.data(), buf.size());
    REQUIRE(recs.size() == 1);
    REQUIRE(recs[0].name == "MOVE_00");
}

// =========================================================================
// Tripwires: each should fail when the corresponding 1-line change to the
// scanner is applied. Names them by mutation to make the linkage explicit.
// =========================================================================

TEST_CASE("scan_legacy_clip_records: TRIPWIRE-H reads start at offset +290, not +288/+292") {
    // If someone mis-adjusts kStartOffset by +/- 2, the uint16 LE read
    // falls on padding bytes which decode as 0 -- and the record would
    // also get dropped by the end >= start check unless end happens to
    // match. Set a distinctive start value at exactly +290 and verify.
    auto rec = make_record(0, "PIN", 1234, 5678);
    auto recs = scan_legacy_clip_records(rec.data(), rec.size());
    REQUIRE(recs.size() == 1);
    REQUIRE(recs[0].start_frame == 1234);
}

TEST_CASE("scan_legacy_clip_records: TRIPWIRE-I reads end at offset +294, not +292/+296") {
    // Symmetric to TRIPWIRE-H. If kEndOffset is wrong, end reads as 0,
    // which is < start (=1), and the record gets dropped silently.
    auto rec = make_record(0, "PIN", 1, 9999);
    auto recs = scan_legacy_clip_records(rec.data(), rec.size());
    REQUIRE(recs.size() == 1);
    REQUIRE(recs[0].end_frame == 9999);
}

TEST_CASE("scan_legacy_clip_records: TRIPWIRE-J record stride is 342, not 340/344") {
    // Two back-to-back records must both decode. If kRecordStride drifts
    // by 2, the second record's Class_ID falls 2 bytes off where the
    // scanner expects, and only the first record decodes.
    auto buf = concat({
        make_record(0, "FIRST",  1, 10),
        make_record(1, "SECOND", 11, 20),
    });
    auto recs = scan_legacy_clip_records(buf.data(), buf.size());
    REQUIRE(recs.size() == 2);
    REQUIRE(recs[0].name == "FIRST");
    REQUIRE(recs[1].name == "SECOND");
}

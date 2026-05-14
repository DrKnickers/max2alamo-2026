#pragma once

// Pure-C++ scanner for Phase 11c -- decodes the legacy Petroglyph
// max2alamo.dle Utility plugin's clip-list records from a raw .max
// file byte buffer. No Max-SDK dependency so the format library can
// unit-test the decoder against fixture bytes.
//
// Background: the legacy Utility plugin stored each named animation
// clip as a 342-byte appData record. Each record begins with the
// 8-byte Class_ID(0x70a24090, 0x60c90f03) (recovered Phase 11a via
// MSVC RTTI walk of UaW Max 9 binary; byte-identical across EaW Max
// 6/8 and FoC Max 8 plugin variants). Within the record:
//   +12  uint32 LE  -- sequential index
//   +34  16 bytes   -- null-terminated ASCII clip name
//   +290 uint16 LE  -- start frame
//   +294 uint16 LE  -- end frame
//
// Clips share a single concatenated timeline -- each record's start
// equals the previous record's end + 1, with the first clip starting
// at frame 1 (frame 0 is intentionally unused, matching Mike
// Lankamp's alamo2max.ms importer convention).
//
// Frame range pinpointed empirically Phase 11c by byte-differential
// on CIS_SBD.max (8 clips, simpler scene leaves rest of body zeroed).
// Cross-validated on EI_SNOWTROOPER.max + Stormtrooper.max (60 each).
// Decoder methodology lives in
// `re/output/phase11_research/11a_legacy_classid_breakthrough.md`.

#include <cstddef>
#include <string>
#include <vector>

namespace alamo_format {

struct LegacyClipRecord {
    int index;         // +12  sequential index, 0..N-1 in discovery order
    std::string name;  // +34  null-terminated ASCII (max 16 chars)
    int start_frame;   // +290 uint16 LE
    int end_frame;     // +294 uint16 LE
};

// Scan a raw .max byte buffer for legacy AlamoUtility clip records.
// Returns the records in file order. Empty result means no records
// (modern Alamo file, non-Alamo .max, or simply a scene with no clips).
//
// Records overlapping the end of the buffer or with start > end are
// dropped silently -- callers see only well-formed records. The
// caller is responsible for the sequential-timeline invariant if it
// matters to them (the importer translates records 1:1 regardless).
std::vector<LegacyClipRecord>
scan_legacy_clip_records(const unsigned char* data, std::size_t size);

}  // namespace alamo_format

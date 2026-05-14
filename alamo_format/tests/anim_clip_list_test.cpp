// Catch2 tests for the Phase 11b.2 pure-C++ clip-list logic. Every
// function under test is a pure transform on strings/integers; no
// Max-SDK dependency. Tripwires D/E/F (see plan) flow from these
// assertions -- each must fail when the corresponding one-line
// mutation is applied.

#include <catch2/catch_test_macros.hpp>

#include "alamo_format/anim_clip_list.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

using alamo_format::AnimClipList;
using alamo_format::parse_clip_list;
using alamo_format::format_clip_list;
using alamo_format::validate_clip_name;
using alamo_format::clip_start_prop_key;
using alamo_format::clip_end_prop_key;
using alamo_format::navigate_clip_index;
using alamo_format::clip_range_union;

// =========================================================================
// parse_clip_list
// =========================================================================

TEST_CASE("parse_clip_list: empty string yields empty list") {
    auto p = parse_clip_list("");
    REQUIRE(p.names.empty());
}

TEST_CASE("parse_clip_list: single name yields 1-element list") {
    auto p = parse_clip_list("WALK");
    REQUIRE(p.names == std::vector<std::string>{"WALK"});
}

TEST_CASE("parse_clip_list: pipe-delimited multi-name list") {
    auto p = parse_clip_list("WALK|ATTACK|IDLE");
    REQUIRE(p.names == std::vector<std::string>{"WALK", "ATTACK", "IDLE"});
}

TEST_CASE("parse_clip_list: empty fields between pipes are dropped") {
    // Matches scene_walker.cpp's split_clip_names behavior so the rollout
    // can't disagree with the walker about what the list contains.
    auto p = parse_clip_list("WALK||ATTACK");
    REQUIRE(p.names == std::vector<std::string>{"WALK", "ATTACK"});
}

TEST_CASE("parse_clip_list: trims ASCII whitespace per field") {
    auto p = parse_clip_list(" WALK | ATTACK \t| IDLE ");
    REQUIRE(p.names == std::vector<std::string>{"WALK", "ATTACK", "IDLE"});
}

TEST_CASE("parse_clip_list: trailing pipe drops trailing empty") {
    auto p = parse_clip_list("WALK|");
    REQUIRE(p.names == std::vector<std::string>{"WALK"});
}

TEST_CASE("parse_clip_list: leading pipe drops leading empty") {
    auto p = parse_clip_list("|WALK");
    REQUIRE(p.names == std::vector<std::string>{"WALK"});
}

TEST_CASE("parse_clip_list: whitespace-only field is dropped") {
    auto p = parse_clip_list("WALK| |ATTACK");
    REQUIRE(p.names == std::vector<std::string>{"WALK", "ATTACK"});
}

// =========================================================================
// format_clip_list
// =========================================================================

TEST_CASE("format_clip_list: empty list yields empty string") {
    REQUIRE(format_clip_list({}) == "");
}

TEST_CASE("format_clip_list: single name yields the name") {
    AnimClipList c;
    c.names = {"WALK"};
    REQUIRE(format_clip_list(c) == "WALK");
}

TEST_CASE("format_clip_list: multi name is pipe-joined") {
    AnimClipList c;
    c.names = {"WALK", "ATTACK", "IDLE"};
    // Tripwire F: changing the delimiter from "|" to "," breaks this.
    REQUIRE(format_clip_list(c) == "WALK|ATTACK|IDLE");
}

TEST_CASE("format_clip_list round-trips parse_clip_list on canonical input") {
    const std::string canonical = "WALK|ATTACK|IDLE";
    REQUIRE(format_clip_list(parse_clip_list(canonical)) == canonical);
}

// =========================================================================
// validate_clip_name -- accepts
// =========================================================================

TEST_CASE("validate_clip_name accepts 'WALK'") {
    REQUIRE(validate_clip_name("WALK", {}).empty());
}

TEST_CASE("validate_clip_name accepts 'ATTACK_00'") {
    REQUIRE(validate_clip_name("ATTACK_00", {}).empty());
}

TEST_CASE("validate_clip_name accepts single-letter 'A'") {
    REQUIRE(validate_clip_name("A", {}).empty());
}

TEST_CASE("validate_clip_name accepts mixed underscore + digits") {
    REQUIRE(validate_clip_name("X_1_Y_2", {}).empty());
}

TEST_CASE("validate_clip_name accepts real legacy name 'ATTACKFLINCHB_00'") {
    // From EI_SNOWTROOPER.max -- one of the 60 clip records the legacy
    // max2alamo.dle plugin authored. Validates against vanilla naming.
    REQUIRE(validate_clip_name("ATTACKFLINCHB_00", {}).empty());
}

// =========================================================================
// validate_clip_name -- rejects
// =========================================================================

TEST_CASE("validate_clip_name rejects empty string") {
    auto err = validate_clip_name("", {});
    REQUIRE(!err.empty());
    REQUIRE(err.find("empty") != std::string::npos);
}

TEST_CASE("validate_clip_name rejects names containing '|'") {
    auto err = validate_clip_name("FOO|BAR", {});
    REQUIRE(!err.empty());
}

TEST_CASE("validate_clip_name rejects names containing a space") {
    auto err = validate_clip_name("FOO BAR", {});
    REQUIRE(!err.empty());
}

TEST_CASE("validate_clip_name rejects lowercase") {
    // Tripwire D: weakening regex to [A-Za-z0-9_] makes this pass.
    auto err = validate_clip_name("walk", {});
    REQUIRE(!err.empty());
    REQUIRE(err.find("uppercase") != std::string::npos);
}

TEST_CASE("validate_clip_name rejects mixed case") {
    REQUIRE(!validate_clip_name("Walk", {}).empty());
    REQUIRE(!validate_clip_name("WALk", {}).empty());
}

TEST_CASE("validate_clip_name rejects names starting with a digit") {
    auto err = validate_clip_name("0WALK", {});
    REQUIRE(!err.empty());
    REQUIRE(err.find("digit") != std::string::npos);
}

TEST_CASE("validate_clip_name rejects duplicate of existing") {
    AnimClipList existing;
    existing.names = {"WALK", "ATTACK"};
    auto err = validate_clip_name("WALK", existing);
    REQUIRE(!err.empty());
    REQUIRE(err.find("exists") != std::string::npos);
}

TEST_CASE("validate_clip_name allows non-duplicate even with existing list") {
    AnimClipList existing;
    existing.names = {"WALK", "ATTACK"};
    REQUIRE(validate_clip_name("IDLE", existing).empty());
}

TEST_CASE("validate_clip_name rejects names longer than 64 chars") {
    // 65 'A's
    std::string too_long(65, 'A');
    auto err = validate_clip_name(too_long, {});
    REQUIRE(!err.empty());
}

TEST_CASE("validate_clip_name accepts exactly 64 chars") {
    std::string just_right(64, 'A');
    REQUIRE(validate_clip_name(just_right, {}).empty());
}

TEST_CASE("validate_clip_name rejects non-ASCII characters") {
    // UTF-8 alpha (Greek): 0xCE 0xB1
    std::string greek_alpha = "\xCE\xB1lpha";
    REQUIRE(!validate_clip_name(greek_alpha, {}).empty());
}

TEST_CASE("validate_clip_name rejects non-printable ASCII") {
    REQUIRE(!validate_clip_name(std::string("WALK\tX"), {}).empty());
    REQUIRE(!validate_clip_name(std::string("WALK\nX"), {}).empty());
}

// =========================================================================
// clip_start_prop_key / clip_end_prop_key
// =========================================================================

TEST_CASE("clip_start_prop_key follows Alamo_Anim_<NAME>_Start convention") {
    REQUIRE(clip_start_prop_key("WALK") == "Alamo_Anim_WALK_Start");
}

TEST_CASE("clip_end_prop_key follows Alamo_Anim_<NAME>_End convention") {
    REQUIRE(clip_end_prop_key("WALK") == "Alamo_Anim_WALK_End");
}

TEST_CASE("clip_start_prop_key preserves clip-name casing exactly") {
    // Permissive on load: legacy lowercase names round-trip through the
    // key formatter (validator rejects them only at Add time).
    REQUIRE(clip_start_prop_key("walk") == "Alamo_Anim_walk_Start");
}

// =========================================================================
// navigate_clip_index
// =========================================================================

TEST_CASE("navigate_clip_index: empty list returns -1") {
    REQUIRE(navigate_clip_index({}, 0, 1) == -1);
    REQUIRE(navigate_clip_index({}, 0, -1) == -1);
}

TEST_CASE("navigate_clip_index: single-element list wraps to self") {
    AnimClipList c;
    c.names = {"WALK"};
    REQUIRE(navigate_clip_index(c, 0, +1) == 0);
    REQUIRE(navigate_clip_index(c, 0, -1) == 0);
}

TEST_CASE("navigate_clip_index: multi-element list advances forward") {
    AnimClipList c;
    c.names = {"A", "B", "C"};
    REQUIRE(navigate_clip_index(c, 0, +1) == 1);
    REQUIRE(navigate_clip_index(c, 1, +1) == 2);
}

TEST_CASE("navigate_clip_index: multi-element list moves backward") {
    AnimClipList c;
    c.names = {"A", "B", "C"};
    REQUIRE(navigate_clip_index(c, 1, -1) == 0);
    REQUIRE(navigate_clip_index(c, 2, -1) == 1);
}

TEST_CASE("navigate_clip_index: wraps forward at end") {
    // Tripwire E: removing the modulo makes this return 3 (OOB).
    AnimClipList c;
    c.names = {"A", "B", "C"};
    REQUIRE(navigate_clip_index(c, 2, +1) == 0);
}

TEST_CASE("navigate_clip_index: wraps backward at start") {
    AnimClipList c;
    c.names = {"A", "B", "C"};
    REQUIRE(navigate_clip_index(c, 0, -1) == 2);
}

// =========================================================================
// clip_range_union
// =========================================================================

TEST_CASE("clip_range_union: empty input yields nullopt") {
    auto r = clip_range_union({});
    REQUIRE(!r.has_value());
}

TEST_CASE("clip_range_union: single range returns itself") {
    auto r = clip_range_union({{5, 10}});
    REQUIRE(r.has_value());
    REQUIRE(r->first  == 5);
    REQUIRE(r->second == 10);
}

TEST_CASE("clip_range_union: contiguous multi-range") {
    auto r = clip_range_union({{0, 30}, {31, 60}, {61, 90}});
    REQUIRE(r.has_value());
    REQUIRE(r->first  == 0);
    REQUIRE(r->second == 90);
}

TEST_CASE("clip_range_union: non-contiguous spans the gap") {
    // Display All "span of all clips" intentionally INCLUDES the gap.
    auto r = clip_range_union({{10, 20}, {50, 60}});
    REQUIRE(r.has_value());
    REQUIRE(r->first  == 10);
    REQUIRE(r->second == 60);
}

TEST_CASE("clip_range_union: overlapping ranges collapse") {
    auto r = clip_range_union({{0, 40}, {30, 50}});
    REQUIRE(r.has_value());
    REQUIRE(r->first  == 0);
    REQUIRE(r->second == 50);
}

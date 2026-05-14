#pragma once

// Pure-C++ logic for the Phase 11b multi-clip animation convention --
// no Max-SDK dependency. Used by both the Utility-panel rollout
// (Phase 11b.2) and by Phase 11c's legacy `.max` clip-data reader when
// it translates recovered records into the same user-prop convention.

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace alamo_format {

// Parsed view of the rootNode's `Alamo_Anim_Clips` user property.
// Order is the on-disk order which is also the rollout's display order
// and the walker's iteration order.
struct AnimClipList {
    std::vector<std::string> names;
};

// Parse "WALK|ATTACK|IDLE" -> {"WALK", "ATTACK", "IDLE"}. Trims ASCII
// space + tab per field; drops empty / whitespace-only fields. Matches
// scene_walker.cpp's `split_clip_names` behavior so the rollout and
// walker can't disagree about what the list contains.
AnimClipList parse_clip_list(const std::string& list);

// Format an AnimClipList back to the pipe-delimited string. Round-trips
// `parse_clip_list` on well-formed canonical input.
std::string format_clip_list(const AnimClipList& clips);

// Strict authoring validator: returns empty string on accept, or a
// one-line human-readable error otherwise. Rules (must hold all of):
//   - non-empty
//   - length <= 64 (Windows MAX_PATH headroom for .ala filename)
//   - chars in [A-Z0-9_] only
//   - first char not a digit
//   - not already present in `existing` (case-sensitive compare)
// The walker is permissive on load -- this validator is the gate for
// NEW authoring via the Utility panel's Add button.
std::string validate_clip_name(const std::string& name,
                               const AnimClipList& existing);

// User-prop key formatters. Centralized so the rollout and walker
// can't drift on the `Alamo_Anim_<NAME>_Start/_End` convention.
std::string clip_start_prop_key(const std::string& name);
std::string clip_end_prop_key(const std::string& name);

// Index math for <</>> nav. Returns new index after applying delta;
// wraps modulo names.size(). Returns -1 for empty list.
int navigate_clip_index(const AnimClipList& clips, int current, int delta);

// Inclusive frame-range union across all clip ranges. Used by the
// Display All button. Returns nullopt for empty input; otherwise
// (min_start, max_end). Non-contiguous and overlapping ranges both
// collapse to the outer bounds.
std::optional<std::pair<int, int>>
clip_range_union(const std::vector<std::pair<int, int>>& per_clip_ranges);

}  // namespace alamo_format

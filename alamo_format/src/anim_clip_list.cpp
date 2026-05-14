#include "alamo_format/anim_clip_list.h"

#include <algorithm>
#include <cctype>

namespace alamo_format {

namespace {

bool is_clip_char(char c) {
    auto u = static_cast<unsigned char>(c);
    return (u >= 'A' && u <= 'Z') ||
           (u >= '0' && u <= '9') ||
           u == '_';
}

// Trim ASCII whitespace (space + tab) from both ends. We deliberately
// don't trim CR/LF because the walker doesn't either -- if a real clip
// list contains them, we want the validator to reject so the user sees
// the surprise rather than silently swallowing it.
std::string trim_ascii_ws(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

}  // namespace

AnimClipList parse_clip_list(const std::string& list) {
    AnimClipList out;
    std::string cur;
    auto flush = [&] {
        std::string trimmed = trim_ascii_ws(cur);
        if (!trimmed.empty()) out.names.push_back(std::move(trimmed));
        cur.clear();
    };
    for (char c : list) {
        if (c == '|') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

std::string format_clip_list(const AnimClipList& clips) {
    std::string out;
    bool first = true;
    for (const auto& n : clips.names) {
        if (!first) out.push_back('|');
        out += n;
        first = false;
    }
    return out;
}

std::string validate_clip_name(const std::string& name,
                               const AnimClipList& existing) {
    if (name.empty()) return "Clip name is empty.";
    if (name.size() > 64) return "Clip name is too long (max 64 characters).";

    // First-character check: not a digit. This is in addition to the
    // is_clip_char gate below.
    if (name[0] >= '0' && name[0] <= '9') {
        return "Clip name cannot start with a digit.";
    }

    for (char c : name) {
        if (!is_clip_char(c)) {
            // Differentiate the most likely culprits in the error so
            // the user knows what they typed wrong.
            if (c >= 'a' && c <= 'z') {
                return "Clip names use uppercase A-Z, digits 0-9, and underscores.";
            }
            // Reuse the same message for everything else (space,
            // pipe, punctuation, non-ASCII) -- the user sees a
            // consistent rule statement.
            return "Clip names use uppercase A-Z, digits 0-9, and underscores.";
        }
    }

    for (const auto& existing_name : existing.names) {
        if (existing_name == name) {
            return "A clip with that name already exists.";
        }
    }
    return {};
}

std::string clip_start_prop_key(const std::string& name) {
    return "Alamo_Anim_" + name + "_Start";
}

std::string clip_end_prop_key(const std::string& name) {
    return "Alamo_Anim_" + name + "_End";
}

int navigate_clip_index(const AnimClipList& clips, int current, int delta) {
    const int n = static_cast<int>(clips.names.size());
    if (n == 0) return -1;
    // Modulo arithmetic that handles negative delta correctly.
    int idx = current + delta;
    idx %= n;
    if (idx < 0) idx += n;
    return idx;
}

std::optional<std::pair<int, int>>
clip_range_union(const std::vector<std::pair<int, int>>& per_clip_ranges) {
    if (per_clip_ranges.empty()) return std::nullopt;
    int lo = per_clip_ranges[0].first;
    int hi = per_clip_ranges[0].second;
    for (std::size_t i = 1; i < per_clip_ranges.size(); ++i) {
        lo = std::min(lo, per_clip_ranges[i].first);
        hi = std::max(hi, per_clip_ranges[i].second);
    }
    return std::make_pair(lo, hi);
}

}  // namespace alamo_format

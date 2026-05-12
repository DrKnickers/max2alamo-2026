// alo_roundtrip: read an .alo file, re-serialize it via the chunk-tree
// reader/writer pair, and compare the result byte-for-byte against the
// original.
//
// Usage:
//   alo_roundtrip <file>              one file, prints PASS/FAIL with diff offset
//   alo_roundtrip --dir <directory>   recurse over .alo and .ala files, summarize
//
// Exit status: 0 if everything matches, 1 otherwise. Designed as the
// correctness oracle for the format library — every byte should round-trip.

#include "alamo_format/chunk_tree.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    f.unsetf(std::ios::skipws);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

struct DiffResult {
    bool match = false;
    std::size_t first_diff_offset = 0;
    std::size_t original_size = 0;
    std::size_t rewritten_size = 0;
};

DiffResult compare_bytes(const std::vector<std::uint8_t>& a,
                         const std::vector<std::uint8_t>& b) {
    DiffResult r;
    r.original_size = a.size();
    r.rewritten_size = b.size();
    const std::size_t common = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) {
            r.first_diff_offset = i;
            r.match = false;
            return r;
        }
    }
    if (a.size() != b.size()) {
        r.first_diff_offset = common;
        r.match = false;
        return r;
    }
    r.match = true;
    return r;
}

bool roundtrip_one(const std::string& path, bool verbose) {
    try {
        auto original = read_file(path);
        auto tree = alamo_format::read_chunk_tree(original.data(), original.size());
        auto rewritten = alamo_format::write_chunk_tree(tree);
        auto diff = compare_bytes(original, rewritten);
        if (diff.match) {
            if (verbose) std::printf("PASS  %s  (%zu bytes)\n", path.c_str(), original.size());
            return true;
        }
        std::printf("FAIL  %s  first diff @ offset %zu  (sizes: orig=%zu, rewritten=%zu)\n",
                    path.c_str(), diff.first_diff_offset, diff.original_size, diff.rewritten_size);
        // Print a small hex context window around the diff
        const std::size_t ctx = 8;
        const std::size_t start = (diff.first_diff_offset >= ctx) ? diff.first_diff_offset - ctx : 0;
        const std::size_t end_a = std::min(diff.original_size,   diff.first_diff_offset + ctx);
        const std::size_t end_b = std::min(diff.rewritten_size,  diff.first_diff_offset + ctx);
        std::printf("        orig: ");
        for (std::size_t i = start; i < end_a; ++i) std::printf("%02X ", original[i]);
        std::printf("\n        rewr: ");
        for (std::size_t i = start; i < end_b; ++i) std::printf("%02X ", rewritten[i]);
        std::printf("\n");
        return false;
    } catch (const std::exception& e) {
        std::printf("FAIL  %s  exception: %s\n", path.c_str(), e.what());
        return false;
    }
}

int roundtrip_dir(const std::string& dir) {
    std::size_t pass = 0, fail = 0;
    std::vector<std::string> first_failures;  // capture up to 10 paths
    for (auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (ext != ".alo" && ext != ".ala") continue;
        const auto p = entry.path().string();
        if (roundtrip_one(p, /*verbose=*/false)) {
            ++pass;
        } else {
            ++fail;
            if (first_failures.size() < 10) first_failures.push_back(p);
        }
    }
    std::printf("\n============================================================\n");
    std::printf("Total: %zu   Pass: %zu   Fail: %zu", pass + fail, pass, fail);
    if (pass + fail > 0) {
        std::printf("   Pass rate: %.2f%%", 100.0 * static_cast<double>(pass)
                                          / static_cast<double>(pass + fail));
    }
    std::printf("\n");
    if (!first_failures.empty()) {
        std::printf("First failures (capped at 10):\n");
        for (const auto& p : first_failures) std::printf("  %s\n", p.c_str());
    }
    return fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "usage:\n"
            "  alo_roundtrip <file.alo>      round-trip a single file\n"
            "  alo_roundtrip --dir <path>    recurse and summarize\n");
        return EXIT_FAILURE;
    }
    std::string arg1 = argv[1];
    if (arg1 == "--dir") {
        if (argc < 3) {
            std::fprintf(stderr, "alo_roundtrip: --dir requires a path\n");
            return EXIT_FAILURE;
        }
        return roundtrip_dir(argv[2]);
    }
    return roundtrip_one(arg1, /*verbose=*/true) ? EXIT_SUCCESS : EXIT_FAILURE;
}

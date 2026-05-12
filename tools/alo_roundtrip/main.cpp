// alo_roundtrip: read an .alo file, re-serialize it, and report byte-diff stats.
//
// This is the correctness oracle the Max plugin relies on later. Implemented
// in Phase 2 once both reader and writer land.

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    (void)argv;
    if (argc < 2) {
        std::fprintf(stderr, "usage: alo_roundtrip <file.alo>\n");
        return EXIT_FAILURE;
    }
    std::fprintf(stderr, "alo_roundtrip: not yet implemented (Phase 2).\n");
    return EXIT_FAILURE;
}

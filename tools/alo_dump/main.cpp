// alo_dump: print the chunk tree of an .alo file.
//
// Implemented in Phase 1 once chunk_reader / alo_reader land. For Phase 0
// this is a stub so CI has something to compile and link against the format lib.

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: alo_dump <file.alo>\n");
        return EXIT_FAILURE;
    }
    std::fprintf(stderr, "alo_dump: not yet implemented (Phase 1).\n");
    return EXIT_FAILURE;
}

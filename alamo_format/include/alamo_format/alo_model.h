#pragma once

// In-memory representation of a parsed .alo file.
//
// Phase 1 fills these structs by reading; Phase 2 serializes them back out;
// Phase 4+ populates them from a 3ds Max scene traversal.
//
// Kept as POD-ish structs (no virtuals, no inheritance) so the format library
// stays agnostic of any host application (Max, Maya, Blender, CLI tools).

namespace alamo_format {

struct AloModel {
    // TODO Phase 1: bones, meshes, lights, hardpoints, proxies, connections.
};

}  // namespace alamo_format

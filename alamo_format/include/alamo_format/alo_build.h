#pragma once

// Build a chunk tree (suitable for write_chunk_tree) from an ExportScene.
// This is the host-agnostic serialization step: ExportScene comes in,
// std::vector<ChunkNode> comes out, no Max SDK involvement.
//
// Phase 4 scope:
//   - 0x200 skeleton with one or more bones (each as 0x202/0x203/0x206)
//   - 0x400 mesh per ExportMesh, with one 0x10100 submesh per material
//   - 0x600 connections binding each mesh to bone 0 (Root)
// Vertex chunks always use rev 2 (chunk 0x10007, 144 bytes per vertex)
// in the full B4I4 layout. The format-name string in 0x10002 controls
// which fields the engine reads -- not the on-disk size. See
// docs/format-notes.md for the layout rationale.

#include "alamo_format/chunk_tree.h"
#include "alamo_format/export_scene.h"

namespace alamo_format {

// Convert an ExportScene into the top-level chunk sequence of an .alo
// file. The result is suitable for write_chunk_tree(...) -> bytes ->
// disk. Pure function; no I/O.
std::vector<ChunkNode> build_alo(const ExportScene& scene);

}  // namespace alamo_format

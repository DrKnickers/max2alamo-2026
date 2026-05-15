#pragma once

// skin_bone_remap: build a per-submesh local-to-global bone index table
// (the 0x10006 chunk's payload) and rewrite the submesh's per-vertex
// `bone_indices` from global skeleton indices to local slot indices.
//
// Why this matters (issue #81):
//
//   AloViewer's loader (`src/Assets/Models.cpp:155`) reads 0x10006 as a
//   flat `uint32[]` of global bone indices into `mesh.skin[]`. The
//   renderer then dereferences each per-vertex `BoneIndices[i]` through
//   this table to get the actual bone matrix:
//     bone_matrix = bones[mesh.skin[BoneIndices[i]]]
//   So when 0x10006 is present, `BoneIndices` are LOCAL slot indices
//   (0..N-1, N = remap size), not global skeleton indices.
//
//   Vanilla content has 779/10,737 submeshes with 0x10006 chunks, and
//   100% of them are on skinned shaders (RSkin* / B4I4*). Our pre-
//   Phase-10.5 walker writes global indices directly into BoneIndices
//   and omits 0x10006, which the renderer interprets as garbage --
//   skinned meshes render as stretched-triangle artifacts.
//
// The helper is a pure in-place transform: walk the submesh's vertices,
// collect unique global bone indices (first-seen order, deterministic),
// build the remap, then rewrite each vertex's `bone_indices[0..3]` to
// the corresponding local slot. After the call, `submesh.skin_bone_remap`
// is populated and `submesh.vertices[*].bone_indices` are local slots.

#include "alamo_format/export_scene.h"

namespace alamo_format {

// Build the local-to-global skin-bone remap for the given submesh and
// rewrite its vertex bone_indices to local slots. Idempotent: re-running
// on an already-remapped submesh (where bone_indices are already small
// local slots) produces a no-op result iff `skin_bone_remap` was already
// populated; otherwise it builds a fresh remap matching the local
// slot values currently in the vertices.
//
// Walker callers should invoke this only for skinned submeshes (those
// whose `vertex_format_name` starts with `alD3dVertRSkin` or
// `alD3dVertB4I4`). For static meshes the call is harmless but the
// writer wouldn't emit 0x10006 anyway since vanilla content only does
// for skinned shaders.
void apply_skin_bone_remap(ExportSubmesh& submesh);

// Check whether `vertex_format_name` indicates a skinned format
// (i.e. RSkin* or B4I4* family). Case-insensitive. Used by the walker
// to decide whether to call `apply_skin_bone_remap`.
bool vertex_format_needs_skin_remap(std::string_view vertex_format_name);

}  // namespace alamo_format

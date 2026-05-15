#pragma once

// vertex_format_selector: maps a PG shader filename (e.g.
// "MeshBumpColorize.fx") to the canonical vertex-format-name string
// (e.g. "alD3dVertNU2U3U3") to emit in the `0x10002` chunk for that
// shader's submeshes, and validates strings against AloViewer's
// recognized set.
//
// Why this matters (issue #75):
//
//   AloViewer's renderer (and, per empirical evidence, the shipping
//   EaW/FoC engine) uses `_stricmp` lookup into a 15-entry
//   `VertexFormatNames` table to bind the GPU vertex declaration —
//   the `0x10002` string IS load-bearing. The vertex chunk ID
//   (`0x10005` legacy vs `0x10007` rev 2) only informs the loader's
//   byte-decode path; AloViewer normalizes both to a 144-byte
//   `MASTER_VERTEX` post-load. So a fresh export tagged with the
//   wrong format string renders broken at the shader-input level
//   (skinned meshes collapse, bump-mapped meshes miss the
//   tangent-frame binding, etc.).
//
// Source of truth:
//
//   Per-shader format mapping is the most-common vanilla pairing
//   from a corpus survey of 10,737 vanilla submeshes
//   (`scripts/survey_vertex_formats.py`). The corpus shows a strict
//   1-to-1 shader↔format-string convention across PG's shipping
//   content, so the mapping is well-defined. For shaders absent
//   from the corpus, we infer from naming convention (RSkin*
//   → skinned variant, *Bump* → tangent variant, etc.) and
//   cross-check against the `_ALAMO_VERTEX_TYPE` directive in our
//   own stub `.fx` files at `shaders/max-preview/`.
//
// The recognized-format-name validator checks against AloViewer's
// authoritative 15-entry `VertexFormatNames` table from
// `src/RenderEngine/DirectX9/VertexFormats.cpp`; comparison is
// case-insensitive (AloViewer uses `_stricmp`).

#include <string>
#include <string_view>

namespace alamo_format::vertex_format_selector {

// Return the canonical vertex-format-name string for a known stock PG
// shader. Returns an empty string for shaders not in the table —
// caller falls back to a sensible default (typically the basic
// `alD3dVertNU2`) or routes through the per-mesh `_ALAMO_VERTEX_TYPE`
// user-prop override (PR C).
std::string default_vertex_format_for_shader(std::string_view shader_name);

// Check whether `name` is one of the 15 vertex-format names AloViewer's
// renderer recognizes. Case-insensitive. Used by the writer to emit a
// warning when the walker (or per-mesh override path) hands us a name
// the renderer wouldn't bind — those would silently fall back to a
// generic / wrong declaration at render time.
bool is_recognized_vertex_format(std::string_view name);

}  // namespace alamo_format::vertex_format_selector

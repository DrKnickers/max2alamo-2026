#pragma once

// shader_table: maps a PG shader filename (e.g. "MeshBumpColorize.fx") to
// the canonical ordered list of (name, kind) parameter entries that the
// engine expects in its 0x10100 material chunk.
//
// Source of truth:
//   1. Gaukler's `material_parameter_dict` from the Blender ALAMO plugin
//      <https://github.com/Gaukler/Blender-ALAMO-Plugin/blob/master/io_alamo_tools/settings.py>
//      -- defines which parameter names are written per shader.
//   2. Empirical observation of vanilla .alo chunk trees -- confirms the
//      types (FLOAT vs FLOAT4) and the canonical order of emission.
//
// All scalar / vector params in this table are always emitted by the
// writer (with their default values if the source material did not
// override them), per the vanilla convention. Texture params are only
// emitted when the source material provides a non-empty filename.

#include "alamo_format/export_scene.h"

#include <cstddef>
#include <string_view>

namespace alamo_format::shader_table {

// One static entry in the per-shader template list. `default_value4` is
// used for Float and Float4 kinds; for Texture entries it is ignored
// (writer omits the chunk when the source filename is empty).
struct ParamSpec {
    std::string_view     name;
    MaterialParam::Kind  kind;
    std::array<float, 4> default_value4{0.f, 0.f, 0.f, 0.f};
};

// Non-owning pointer + size pair -- a minimal std::span replacement so
// the format library can stay on C++17.
struct ParamSpecList {
    const ParamSpec* data = nullptr;
    std::size_t      size = 0;

    const ParamSpec* begin() const noexcept { return data; }
    const ParamSpec* end()   const noexcept { return data + size; }
    bool             empty() const noexcept { return size == 0; }
};

// Returns the canonical parameter template for `shader_filename`. If the
// shader is not in the table, returns an empty list. Note: an empty list
// can also be returned for known shaders that have zero params
// (alDefault) -- use `contains()` to disambiguate when the distinction
// matters (the writer needs it to choose between "emit no params" and
// "fall back to BaseTexture-only").
ParamSpecList params_for(std::string_view shader_filename);

// True iff `shader_filename` has an entry in the table (even an empty
// one). Lets callers distinguish "known shader, no params" from
// "unknown shader".
bool contains(std::string_view shader_filename);

}  // namespace alamo_format::shader_table

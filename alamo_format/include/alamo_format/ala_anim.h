#pragma once

// In-memory representation of a parsed .ala animation file.
//
// Reference: https://modtools.petrolution.net/docs/AlaFileFormat
//
// Quaternion compression note (per Blender-ALAMO-Plugin/io_alamo_tools/export_ala.py):
//   each component stored as int16 = round(component * 32767), order XYZW.
// We mirror that convention in ala_writer.cpp during Phase 8.

namespace alamo_format {

struct AlaAnimation {
    // TODO Phase 8: bone tracks, position/rotation/scale keyframes, visibility tracks.
};

}  // namespace alamo_format

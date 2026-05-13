#pragma once

// Walk the active 3ds Max scene through IGame and produce an
// alamo_format::ExportScene snapshot. This is the only place in the
// repo that touches Max's scene-traversal API; everything past the
// resulting ExportScene is host-agnostic and unit-testable.

#include "alamo_format/ala_anim.h"
#include "alamo_format/export_scene.h"

#include <string>

class Interface;  // <maxapi.h> -- forward-declared to avoid pulling Max.h here

namespace max2alamo {

// Populates `out_scene` with the current scene's exportable meshes /
// bones / lights / proxies, and `out_anim` with sampled per-frame
// rotation tracks (Phase 8b scope: FoC format, rotation only).
//
// Animation sampling reads `Alamo_Anim_Start` / `Alamo_Anim_End` /
// `Alamo_Anim_Name` user properties from the scene-root node. If those
// are absent or yield an invalid range, `out_anim` stays default-
// constructed (no animation data; the caller should skip the .ala
// write).
//
// On failure, returns false and writes a one-line description into
// `out_error`. Always seeds `out_scene` with one synthetic Root bone
// via ExportScene::with_root_bone() before walking, even on failure --
// callers can choose to ship a meshless-but-well-formed file or abort.
bool walk_scene(Interface*                  max_interface,
                alamo_format::ExportScene&  out_scene,
                alamo_format::AlaAnimation& out_anim,
                std::string&                out_error);

// Walk the scene a second time and emit a human-readable diagnostic
// report describing what the walker saw on the material side -- node
// name, material class, texture-map slot IDs and filenames, DXMaterial
// effect file. Written verbatim into `out_log` (which is appended to;
// caller controls flushing to disk). Used by DoExport to drop a
// .export.log next to the .alo for debugging "why did we extract THIS
// texture?" questions.
void log_material_diagnostics(Interface* max_interface, std::string& out_log);

// Phase 7b.1: append a summary of the just-walked ExportScene (mesh /
// light / proxy counts + per-light field dump) to the .export.log.
// Driven from the ExportScene rather than the live Max scene so the
// numbers in the log match exactly what got written to disk.
void log_scene_summary(const alamo_format::ExportScene& scene, std::string& out_log);

}  // namespace max2alamo

#pragma once

// Walk the active 3ds Max scene through IGame and produce an
// alamo_format::ExportScene snapshot. This is the only place in the
// repo that touches Max's scene-traversal API; everything past the
// resulting ExportScene is host-agnostic and unit-testable.

#include "alamo_format/ala_anim.h"
#include "alamo_format/export_scene.h"

#include <string>
#include <vector>

class Interface;  // <maxapi.h> -- forward-declared to avoid pulling Max.h here

namespace max2alamo {

// One clip's sampled animation. The clip `name` is used by the caller
// to derive the per-clip .ala filename (`<asset>_<NAME>.ala`); the
// .ala format itself has no on-disk clip-name field (Mike Lankamp's
// importer auto-discovers via the `<basename>_*.ALA` glob -- see
// re/output/phase11_research/11a_findings.md).
//
// `name` is empty for the single-clip un-suffixed back-compat path;
// the exporter then writes a bare `<asset>.ala`.
struct ClipAnimation {
    std::string                 name;
    alamo_format::AlaAnimation  anim;
};

// Populates `out_scene` with the current scene's exportable meshes /
// bones / lights / proxies, and `out_clips` with one ClipAnimation per
// authored animation clip.
//
// Animation sampling reads clip metadata from the scene-root node's
// user properties (Phase 11b):
//   1. If `Alamo_Anim_Clips` is set (pipe-delimited clip-name list),
//      each name `<NAME>` is paired with `Alamo_Anim_<NAME>_Start` /
//      `_End` user props for its frame range. Per-clip failures (e.g.
//      missing `_Start`) are logged and skipped; successful clips
//      still emit.
//   2. Else if un-suffixed `Alamo_Anim_Start` / `_End` / `_Name` are
//      set (Phase 8b/c/d back-compat), the walker emits a single clip
//      with empty name (-> bare `<asset>.ala` filename).
//   3. Else `out_clips` stays empty (no .ala emitted).
//
// On failure, returns false and writes a one-line description into
// `out_error`. Always seeds `out_scene` with one synthetic Root bone
// via ExportScene::with_root_bone() before walking, even on failure --
// callers can choose to ship a meshless-but-well-formed file or abort.
bool walk_scene(Interface*                  max_interface,
                alamo_format::ExportScene&  out_scene,
                std::vector<ClipAnimation>& out_clips,
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

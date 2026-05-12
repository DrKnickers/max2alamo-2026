#pragma once

// Walk the active 3ds Max scene through IGame and produce an
// alamo_format::ExportScene snapshot. This is the only place in the
// repo that touches Max's scene-traversal API; everything past the
// resulting ExportScene is host-agnostic and unit-testable.

#include "alamo_format/export_scene.h"

#include <string>

class Interface;  // <maxapi.h> -- forward-declared to avoid pulling Max.h here

namespace max2alamo {

// Populates `out` with the current scene's exportable meshes (Phase 4
// scope: top-level mesh nodes, single-material per submesh, no skin,
// no transforms baked in beyond what IGame returns in object space).
//
// On failure, returns false and writes a one-line description into
// `out_error`. Always seeds `out` with one synthetic Root bone via
// ExportScene::with_root_bone() before walking, even on failure --
// callers can choose to ship a meshless-but-well-formed file or abort.
bool walk_scene(Interface*                  max_interface,
                alamo_format::ExportScene&  out,
                std::string&                out_error);

}  // namespace max2alamo

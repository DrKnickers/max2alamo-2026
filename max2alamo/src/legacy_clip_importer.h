#pragma once

// Phase 11c: translate legacy Petroglyph max2alamo.dle clip records
// (stored in legacy .max files' appData stream) into Phase 11b's
// `Alamo_Anim_Clips` + per-clip `Alamo_Anim_<NAME>_Start/_End` user-prop
// convention. The Animation Settings rollout (Phase 11b.2) and walker
// (Phase 11b.1) then surface the imported clips automatically.
//
// Strategy 1: byte-scan the .max file from disk on NOTIFY_FILE_POST_OPEN.
// The pure-C++ scanner (`alamo_format::scan_legacy_clip_records`) does
// the heavy lifting; this module reads the file, calls the scanner,
// translates results to user props, and extends animationRange.

#include <Max.h>

namespace max2alamo {

// Read the file at `file_path` (UTF-16 absolute path), scan for legacy
// clip records, translate to Phase 11b user props on `root_node`, and
// extend Max's animationRange to cover the imported ranges.
//
// Policy: if `root_node` already has a non-empty `Alamo_Anim_Clips`
// user prop, this is a no-op (modern Phase 11b authoring wins -- one-way
// upgrade legacy -> modern, never the reverse, never overwrite).
//
// Returns the number of clips imported. 0 means: no records, or modern
// data already present, or the file couldn't be read.
int ImportLegacyClipsFromFile(const TCHAR* file_path,
                              INode* root_node,
                              Interface* ip);

// Convenience entry point used by the file-open notification callback.
// Resolves the current file path + root node from Interface and runs
// the importer. Logs the result to the MAXScript listener so users see
// "Imported N legacy animation clips" feedback after opening a legacy
// file.
void MaybeImportLegacyClipsFromCurrentScene(Interface* ip);

}  // namespace max2alamo

#pragma once

// Phase 11b.2: Animation Settings rollout backend.
//
// Wires the existing Animation Settings rollout (visual layout shipped
// in Phase 7-era Utility-UI work; backend stubbed) to the Phase 11b.1
// Alamo_Anim_Clips + Alamo_Anim_<NAME>_Start/_End user-prop convention
// on the scene root node.
//
// Editor model (user-confirmed during brainstorming):
//   - User props on rootNode are AUTHORITATIVE.
//   - Combo selection LOADS picked clip's [Start, End] into both the
//     Start/End spinners AND Max's animationRange.
//   - Spinner CC_SPINNER_BUTTONUP commits write back to props + re-applies
//     to animationRange.
//   - Manual time-slider scrubs are PREVIEW ONLY -- they don't mutate
//     clip data.
//
// All writes are bracketed by theHold.Begin() / theHold.Accept(name) so
// each user action becomes one ctrl-Z entry on Max's undo stack.

#include <Max.h>

namespace max2alamo {

// Rollout DlgProc registered by AlamoUtility::BeginEditParams.
INT_PTR CALLBACK AnimationSettingsDlgProc(HWND hDlg, UINT msg,
                                          WPARAM wParam, LPARAM lParam);

// Repopulate combo + spinners + animationRange from the current
// rootNode's user props. Called from WM_INITDIALOG AND from the
// NOTIFY_FILE_POST_OPEN callback so the rollout reflects scene state
// after file loads. Safe to call with hDlg == nullptr (no-op).
void RefreshAnimationSettings(HWND hDlg);

}  // namespace max2alamo

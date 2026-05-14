#include "animation_settings_dlg.h"
#include "alamo_utility.h"

#include "../resources/utility_resource.h"

#include "alamo_format/anim_clip_list.h"

#include <Max.h>
#include <maxapi.h>
#include <hold.h>
#include <custcont.h>   // ISpinnerControl, GetISpinner, SetupIntSpinner
#include <notify.h>     // NOTIFY_FILE_POST_OPEN, NOTIFY_TIMERANGE_CHANGE

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

extern HINSTANCE g_h_instance;

namespace max2alamo {

namespace {

// ---- UTF-8 <-> wide helpers (file-local; mirrors alamo_utility.cpp's) -----

std::string to_utf8(const TCHAR* s)
{
    if (!s) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring to_wide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                  static_cast<int>(s.size()),
                                  nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), len);
    return out;
}

// ---- Dialog-context helpers ----------------------------------------------

AlamoUtility* GetUtility(HWND hDlg)
{
    return reinterpret_cast<AlamoUtility*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
}

void EnableCtrl(HWND hDlg, int id, bool enable)
{
    HWND h = GetDlgItem(hDlg, id);
    if (h) EnableWindow(h, enable ? TRUE : FALSE);
}

// Re-entrancy guard: programmatic CB_SETCURSEL fires CBN_SELCHANGE
// synchronously, identical to a user click. During refresh we don't
// want the SELCHANGE handler to fire (it would re-snap animationRange
// and re-write props pointlessly). Stack-scoped flag.
bool& combo_suppress_flag() { static bool f = false; return f; }
struct SuppressComboGuard {
    SuppressComboGuard()  { combo_suppress_flag() = true; }
    ~SuppressComboGuard() { combo_suppress_flag() = false; }
};

// ---- User-prop I/O on rootNode -------------------------------------------

INode* GetRootNode(AlamoUtility* util)
{
    if (!util || !util->m_ip) return nullptr;
    return util->m_ip->GetRootNode();
}

std::string ReadStringProp(INode* root, const std::string& key)
{
    if (!root) return {};
    auto wkey = to_wide(key);
    MSTR value;
    if (!root->GetUserPropString(const_cast<TCHAR*>(wkey.c_str()), value)) return {};
    return to_utf8(value.data());
}

void WriteStringProp(INode* root, const std::string& key, const std::string& value)
{
    if (!root) return;
    auto wkey   = to_wide(key);
    auto wvalue = to_wide(value);
    root->SetUserPropString(const_cast<TCHAR*>(wkey.c_str()),
                            const_cast<TCHAR*>(wvalue.c_str()));
}

int ReadIntProp(INode* root, const std::string& key, int dflt)
{
    if (!root) return dflt;
    auto wkey = to_wide(key);
    int v = dflt;
    if (root->GetUserPropInt(const_cast<TCHAR*>(wkey.c_str()), v)) return v;
    return dflt;
}

void WriteIntProp(INode* root, const std::string& key, int value)
{
    if (!root) return;
    auto wkey = to_wide(key);
    root->SetUserPropInt(const_cast<TCHAR*>(wkey.c_str()), value);
}

// ---- Spinner I/O ----------------------------------------------------------

int GetSpinnerValue(HWND hDlg, int spin_id)
{
    if (ISpinnerControl* s = GetISpinner(GetDlgItem(hDlg, spin_id))) {
        int v = s->GetIVal();
        ReleaseISpinner(s);
        return v;
    }
    return 0;
}

void SetSpinnerValue(HWND hDlg, int spin_id, int value)
{
    if (ISpinnerControl* s = GetISpinner(GetDlgItem(hDlg, spin_id))) {
        s->SetValue(value, FALSE);  // FALSE = don't notify (we're refreshing)
        ReleaseISpinner(s);
    }
}

// ---- Animation range ------------------------------------------------------

// Set Max's animationRange in frames and broadcast NOTIFY_TIMERANGE_CHANGE
// so the time-slider UI refreshes its bounds (Risk #2 from the spec).
void SetAnimRangeAndNotify(Interface* ip, int start_frame, int end_frame)
{
    if (!ip) return;
    const int tpf = ::GetTicksPerFrame();
    Interval iv(start_frame * tpf, end_frame * tpf);
    ip->SetAnimRange(iv);
    BroadcastNotification(NOTIFY_TIMERANGE_CHANGE);
}

int GetAnimRangeEndFrames(Interface* ip)
{
    if (!ip) return 0;
    const int tpf = ::GetTicksPerFrame();
    if (tpf <= 0) return 0;
    Interval iv = ip->GetAnimRange();
    return iv.End() / tpf;
}

// ---- Clip-list I/O -------------------------------------------------------

// Load the clip list as the user sees it in the rollout. Primary path
// reads `Alamo_Anim_Clips`. Back-compat path (Phase 11b.1's un-suffixed
// single-clip convention) kicks in when `Alamo_Anim_Clips` is absent
// but un-suffixed `Alamo_Anim_Start/_End` (+ optional `Alamo_Anim_Name`)
// are set -- we synthesize a one-element list using `Alamo_Anim_Name`
// (or a generic "clip" fallback when name is absent) so the rollout
// surfaces the clip the walker would emit. Mirrors the walker's
// precedence rule from scene_walker.cpp::walk_animations.
alamo_format::AnimClipList LoadClipList(AlamoUtility* util)
{
    INode* root = GetRootNode(util);
    auto list = alamo_format::parse_clip_list(ReadStringProp(root, "Alamo_Anim_Clips"));
    if (!list.names.empty()) return list;

    // Multi-clip absent -- check for un-suffixed back-compat.
    if (!root) return list;
    int start = ReadIntProp(root, "Alamo_Anim_Start", -1);
    int end   = ReadIntProp(root, "Alamo_Anim_End",   -1);
    if (start < 0 || end < 0) return list;  // no back-compat either

    std::string name = ReadStringProp(root, "Alamo_Anim_Name");
    if (name.empty()) name = "clip";  // generic fallback
    list.names.push_back(std::move(name));
    return list;
}

void SaveClipList(AlamoUtility* util, const alamo_format::AnimClipList& clips)
{
    INode* root = GetRootNode(util);
    if (!root) return;
    WriteStringProp(root, "Alamo_Anim_Clips", alamo_format::format_clip_list(clips));
}

// ---- Modal name-prompt sub-dialog ----------------------------------------

struct PromptContext {
    const alamo_format::AnimClipList* existing;
    std::string                       result;  // UTF-8
};

INT_PTR CALLBACK ClipNamePromptDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        SetFocus(GetDlgItem(hDlg, IDC_CLIP_NAME_EDIT));
        return FALSE;  // we set focus ourselves

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDOK) {
            auto* ctx = reinterpret_cast<PromptContext*>(
                GetWindowLongPtr(hDlg, GWLP_USERDATA));
            if (!ctx) { EndDialog(hDlg, IDCANCEL); return TRUE; }
            TCHAR buf[128] = {0};
            GetDlgItemText(hDlg, IDC_CLIP_NAME_EDIT, buf, _countof(buf));
            std::string name = to_utf8(buf);
            std::string err  = alamo_format::validate_clip_name(name, *ctx->existing);
            if (err.empty()) {
                ctx->result = std::move(name);
                EndDialog(hDlg, IDOK);
            } else {
                // Show inline; do NOT close the dialog.
                auto werr = to_wide(err);
                SetDlgItemText(hDlg, IDC_CLIP_NAME_ERROR, werr.c_str());
            }
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

// Open the modal prompt. Returns the new clip name on OK (validated +
// unique against `existing`); empty string on Cancel.
std::string PromptForClipName(HWND parent, const alamo_format::AnimClipList& existing)
{
    PromptContext ctx{&existing, {}};
    INT_PTR rc = DialogBoxParam(g_h_instance,
                                MAKEINTRESOURCE(IDD_ALAMO_CLIP_NAME_PROMPT),
                                parent,
                                ClipNamePromptDlgProc,
                                reinterpret_cast<LPARAM>(&ctx));
    return (rc == IDOK) ? ctx.result : std::string{};
}

// ---- Refresh + handlers --------------------------------------------------

// Load the clip list off rootNode and repopulate the combo. Selects
// `select_name` if present (case-sensitive); otherwise selects index 0.
// Suppresses CBN_SELCHANGE during the rebuild via SuppressComboGuard.
void RebuildComboFromProps(HWND hDlg, AlamoUtility* util,
                           const std::string& select_name)
{
    SuppressComboGuard guard;
    HWND combo = GetDlgItem(hDlg, IDC_ANIM_NAME_COMBO);
    if (!combo) return;
    SendMessage(combo, CB_RESETCONTENT, 0, 0);

    auto clips = LoadClipList(util);
    int sel = -1;
    for (std::size_t i = 0; i < clips.names.size(); ++i) {
        auto w = to_wide(clips.names[i]);
        SendMessage(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
        if (clips.names[i] == select_name) sel = static_cast<int>(i);
    }
    if (clips.names.empty()) {
        SendMessage(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(_T("-- none --")));
        SendMessage(combo, CB_SETCURSEL, 0, 0);
    } else {
        if (sel < 0) sel = 0;
        SendMessage(combo, CB_SETCURSEL, sel, 0);
    }
}

// Get the current combo selection as a UTF-8 string. Empty if no
// clip selected (combo shows "-- none --" sentinel).
std::string CurrentClipName(HWND hDlg, const alamo_format::AnimClipList& clips)
{
    HWND combo = GetDlgItem(hDlg, IDC_ANIM_NAME_COMBO);
    if (!combo) return {};
    int idx = static_cast<int>(SendMessage(combo, CB_GETCURSEL, 0, 0));
    if (idx < 0 || idx >= static_cast<int>(clips.names.size())) return {};
    return clips.names[static_cast<std::size_t>(idx)];
}

void UpdateControlEnableState(HWND hDlg, std::size_t clip_count)
{
    const bool have = clip_count > 0;
    EnableCtrl(hDlg, IDC_ANIM_NAME_COMBO,      have);
    EnableCtrl(hDlg, IDC_ANIM_START_EDIT,      have);
    EnableCtrl(hDlg, IDC_ANIM_START_SPIN,      have);
    EnableCtrl(hDlg, IDC_ANIM_END_EDIT,        have);
    EnableCtrl(hDlg, IDC_ANIM_END_SPIN,        have);
    EnableCtrl(hDlg, IDC_ANIM_PREV,            clip_count > 1);
    EnableCtrl(hDlg, IDC_ANIM_NEXT,            clip_count > 1);
    EnableCtrl(hDlg, IDC_ANIM_DEL,             have);
    EnableCtrl(hDlg, IDC_ANIM_DISPLAY_CURRENT, have);
    EnableCtrl(hDlg, IDC_ANIM_DISPLAY_ALL,     have);
    EnableCtrl(hDlg, IDC_ANIM_ADD,             TRUE);  // always allow Add
}

// Apply the clip currently selected in the combo: load its _Start/_End
// into the spinners and scrub animationRange to match. The "selecting
// a clip adjusts the keyframe range to that clip" behavior the user
// called out explicitly.
// True iff the rootNode is in Phase 11b.1 back-compat mode: no
// Alamo_Anim_Clips, only un-suffixed Alamo_Anim_Start/_End/_Name.
bool IsBackCompatMode(INode* root)
{
    if (!root) return false;
    return ReadStringProp(root, "Alamo_Anim_Clips").empty();
}

void ApplySelectedClip(HWND hDlg, AlamoUtility* util)
{
    auto clips = LoadClipList(util);
    std::string name = CurrentClipName(hDlg, clips);
    if (name.empty()) return;
    INode* root = GetRootNode(util);
    if (!root) return;
    int start, end;
    if (IsBackCompatMode(root)) {
        start = ReadIntProp(root, "Alamo_Anim_Start", 0);
        end   = ReadIntProp(root, "Alamo_Anim_End",   0);
    } else {
        start = ReadIntProp(root, alamo_format::clip_start_prop_key(name), 0);
        end   = ReadIntProp(root, alamo_format::clip_end_prop_key(name),   0);
    }
    SetSpinnerValue(hDlg, IDC_ANIM_START_SPIN, start);
    SetSpinnerValue(hDlg, IDC_ANIM_END_SPIN,   end);
    SetAnimRangeAndNotify(util->m_ip, start, end);
}

// ---- Button handlers -----------------------------------------------------

void HandleSpinnerCommit(HWND hDlg, AlamoUtility* util, int spin_id)
{
    auto clips = LoadClipList(util);
    std::string name = CurrentClipName(hDlg, clips);
    if (name.empty()) return;
    INode* root = GetRootNode(util);
    if (!root) return;

    int start = GetSpinnerValue(hDlg, IDC_ANIM_START_SPIN);
    int end   = GetSpinnerValue(hDlg, IDC_ANIM_END_SPIN);
    // Clamp: end >= start. If the user shoves start past end, push end up.
    if (end < start) {
        if (spin_id == IDC_ANIM_START_SPIN) {
            end = start;
            SetSpinnerValue(hDlg, IDC_ANIM_END_SPIN, end);
        } else {
            start = end;
            SetSpinnerValue(hDlg, IDC_ANIM_START_SPIN, start);
        }
    }

    const TCHAR* label = (spin_id == IDC_ANIM_START_SPIN)
        ? _T("Edit Clip Start Frame")
        : _T("Edit Clip End Frame");
    theHold.Begin();
    if (IsBackCompatMode(root)) {
        // Back-compat scene: write to un-suffixed Alamo_Anim_Start/_End
        // so the walker's back-compat path sees the new values. (If we
        // wrote suffixed keys here, Alamo_Anim_Clips is empty so the
        // walker's precedence rule routes through un-suffixed and the
        // edit would have no export effect.)
        WriteIntProp(root, "Alamo_Anim_Start", start);
        WriteIntProp(root, "Alamo_Anim_End",   end);
    } else {
        WriteIntProp(root, alamo_format::clip_start_prop_key(name), start);
        WriteIntProp(root, alamo_format::clip_end_prop_key(name),   end);
    }
    theHold.Accept(label);

    SetAnimRangeAndNotify(util->m_ip, start, end);
}

void HandleNavigate(HWND hDlg, AlamoUtility* util, int delta)
{
    auto clips = LoadClipList(util);
    if (clips.names.empty()) return;
    HWND combo = GetDlgItem(hDlg, IDC_ANIM_NAME_COMBO);
    int cur = static_cast<int>(SendMessage(combo, CB_GETCURSEL, 0, 0));
    int next = alamo_format::navigate_clip_index(clips, cur, delta);
    if (next < 0) return;
    // SET CURSEL fires CBN_SELCHANGE which will run ApplySelectedClip.
    // We DO want that here -- this is a real user nav action.
    SendMessage(combo, CB_SETCURSEL, next, 0);
    ApplySelectedClip(hDlg, util);
}

void HandleAdd(HWND hDlg, AlamoUtility* util)
{
    auto clips = LoadClipList(util);
    std::string new_name = PromptForClipName(hDlg, clips);
    if (new_name.empty()) return;  // cancelled or empty after validation
    INode* root = GetRootNode(util);
    if (!root) return;

    const int default_end = GetAnimRangeEndFrames(util->m_ip);

    theHold.Begin();
    clips.names.push_back(new_name);
    SaveClipList(util, clips);
    WriteIntProp(root, alamo_format::clip_start_prop_key(new_name), 0);
    WriteIntProp(root, alamo_format::clip_end_prop_key(new_name),   default_end);
    theHold.Accept(_T("Add Animation Clip"));

    RebuildComboFromProps(hDlg, util, new_name);
    UpdateControlEnableState(hDlg, clips.names.size());
    ApplySelectedClip(hDlg, util);
}

void HandleDel(HWND hDlg, AlamoUtility* util)
{
    auto clips = LoadClipList(util);
    std::string name = CurrentClipName(hDlg, clips);
    if (name.empty()) return;

    std::wstring msg = L"Delete animation clip \"" + to_wide(name) + L"\"?";
    if (MessageBox(hDlg, msg.c_str(), L"Alamo Utility",
                   MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    INode* root = GetRootNode(util);
    if (!root) return;

    // Find clip's index BEFORE mutation so we can select the previous
    // clip after refresh.
    auto it = std::find(clips.names.begin(), clips.names.end(), name);
    if (it == clips.names.end()) return;
    const auto removed_idx = static_cast<int>(it - clips.names.begin());
    clips.names.erase(it);

    theHold.Begin();
    SaveClipList(util, clips);
    // Soft-delete via sentinel: walker's Phase 10b contract treats
    // _Start = -1 as "absent" (same as the prop being gone). Avoids
    // brittle SetUserPropBuffer rewrites. See spec Risk #1.
    WriteIntProp(root, alamo_format::clip_start_prop_key(name), -1);
    WriteIntProp(root, alamo_format::clip_end_prop_key(name),   -1);
    theHold.Accept(_T("Delete Animation Clip"));

    std::string select_name;
    if (!clips.names.empty()) {
        int new_idx = std::max(0, removed_idx - 1);
        if (new_idx >= static_cast<int>(clips.names.size())) {
            new_idx = static_cast<int>(clips.names.size()) - 1;
        }
        select_name = clips.names[static_cast<std::size_t>(new_idx)];
    }
    RebuildComboFromProps(hDlg, util, select_name);
    UpdateControlEnableState(hDlg, clips.names.size());
    if (!select_name.empty()) ApplySelectedClip(hDlg, util);
}

void HandleDisplayCurrent(HWND hDlg, AlamoUtility* util)
{
    // Re-apply the currently-selected clip's stored range to
    // animationRange. Idempotent given the editor model -- useful
    // after the user manually scrubbed the time slider.
    ApplySelectedClip(hDlg, util);
}

void HandleDisplayAll(HWND hDlg, AlamoUtility* util)
{
    auto clips = LoadClipList(util);
    if (clips.names.empty()) return;
    INode* root = GetRootNode(util);
    if (!root) return;
    std::vector<std::pair<int,int>> ranges;
    ranges.reserve(clips.names.size());
    for (const auto& n : clips.names) {
        int s = ReadIntProp(root, alamo_format::clip_start_prop_key(n), 0);
        int e = ReadIntProp(root, alamo_format::clip_end_prop_key(n),   0);
        // Skip soft-deleted sentinels.
        if (s < 0 || e < 0) continue;
        ranges.emplace_back(s, e);
    }
    auto unioned = alamo_format::clip_range_union(ranges);
    if (!unioned) return;
    SetAnimRangeAndNotify(util->m_ip, unioned->first, unioned->second);
}

}  // namespace

// ---- Public surface -------------------------------------------------------

void RefreshAnimationSettings(HWND hDlg)
{
    if (!hDlg) return;
    auto* util = GetUtility(hDlg);
    if (!util) return;
    RebuildComboFromProps(hDlg, util, /*select_name=*/{});
    auto clips = LoadClipList(util);
    UpdateControlEnableState(hDlg, clips.names.size());
    if (!clips.names.empty()) ApplySelectedClip(hDlg, util);
}

INT_PTR CALLBACK AnimationSettingsDlgProc(HWND hDlg, UINT msg,
                                          WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        if (auto* util = reinterpret_cast<AlamoUtility*>(lParam)) {
            util->m_hAnimSettings = hDlg;
        }
        // Spinner range: 0..100000 frames is generous; Max's animation
        // range supports up to a few million ticks but in practice
        // Alamo clips are <= a few thousand frames.
        if (ISpinnerControl* s = SetupIntSpinner(hDlg, IDC_ANIM_START_SPIN,
                                                 IDC_ANIM_START_EDIT,
                                                 0, 100000, 0)) {
            ReleaseISpinner(s);
        }
        if (ISpinnerControl* s = SetupIntSpinner(hDlg, IDC_ANIM_END_SPIN,
                                                 IDC_ANIM_END_EDIT,
                                                 0, 100000, 0)) {
            ReleaseISpinner(s);
        }
        RefreshAnimationSettings(hDlg);
        return TRUE;
    }

    case WM_COMMAND: {
        const int id   = LOWORD(wParam);
        const int code = HIWORD(wParam);
        auto* util = GetUtility(hDlg);
        if (!util) return FALSE;

        if (id == IDC_ANIM_NAME_COMBO && code == CBN_SELCHANGE) {
            if (combo_suppress_flag()) return TRUE;
            ApplySelectedClip(hDlg, util);
            return TRUE;
        }
        if (id == IDC_ANIM_PREV) {
            HandleNavigate(hDlg, util, -1);
            return TRUE;
        }
        if (id == IDC_ANIM_NEXT) {
            HandleNavigate(hDlg, util, +1);
            return TRUE;
        }
        if (id == IDC_ANIM_ADD) {
            HandleAdd(hDlg, util);
            return TRUE;
        }
        if (id == IDC_ANIM_DEL) {
            HandleDel(hDlg, util);
            return TRUE;
        }
        if (id == IDC_ANIM_DISPLAY_CURRENT) {
            HandleDisplayCurrent(hDlg, util);
            return TRUE;
        }
        if (id == IDC_ANIM_DISPLAY_ALL) {
            HandleDisplayAll(hDlg, util);
            return TRUE;
        }
        return FALSE;
    }

    // Spinner commit. Per Max SDK custcont.h:
    //   CC_SPINNER_CHANGE:
    //     LOWORD(wParam) = ctrlID
    //     HIWORD(wParam) = TRUE if user is dragging the spinner
    //                      interactively; FALSE on commit (typed
    //                      Enter/Tab, or click on up/down button).
    //     lParam         = ISpinnerControl*
    //   CC_SPINNER_BUTTONUP:
    //     LOWORD(wParam) = ctrlID
    //     HIWORD(wParam) = TRUE on normal release; FALSE if user
    //                      cancelled the spinner drag.
    //
    // We commit on CC_SPINNER_CHANGE/HIWORD=FALSE (text commits +
    // single-click increments) AND on CC_SPINNER_BUTTONUP/HIWORD=TRUE
    // (drag-end). Mid-drag CC_SPINNER_CHANGE/HIWORD=TRUE intermediates
    // are skipped so each user action is one ctrl-Z entry.
    //
    // (Bug history: 11b.2 ship initially listened only to
    // CC_SPINNER_BUTTONUP -- missed the typed-Tab-out commit path.
    // A first fix used `static_cast<BOOL>(lParam)` for the interactive
    // flag, but lParam is the ISpinnerControl* (always non-null) so
    // every message was treated as interactive and skipped.)
    case CC_SPINNER_CHANGE: {
        auto* util = GetUtility(hDlg);
        if (!util) return FALSE;
        const int  id          = LOWORD(wParam);
        const BOOL interactive = static_cast<BOOL>(HIWORD(wParam));
        if (interactive) return FALSE;  // mid-drag intermediate, skip
        if (id == IDC_ANIM_START_SPIN || id == IDC_ANIM_END_SPIN) {
            HandleSpinnerCommit(hDlg, util, id);
            return TRUE;
        }
        return FALSE;
    }
    case CC_SPINNER_BUTTONUP: {
        // Drag-end commit. HIWORD(wParam) = FALSE means the user
        // cancelled (right-clicked during drag); in that case the
        // spinner restores its pre-drag value and we don't write.
        auto* util = GetUtility(hDlg);
        if (!util) return FALSE;
        const int  id        = LOWORD(wParam);
        const BOOL completed = static_cast<BOOL>(HIWORD(wParam));
        if (!completed) return FALSE;  // user cancelled drag
        if (id == IDC_ANIM_START_SPIN || id == IDC_ANIM_END_SPIN) {
            HandleSpinnerCommit(hDlg, util, id);
            return TRUE;
        }
        return FALSE;
    }

    case WM_DESTROY:
        // If a theHold bracket is open due to mid-action teardown, cancel
        // it so the undo stack doesn't end up with a dangling entry.
        if (theHold.Holding()) theHold.Cancel();
        if (auto* util = GetUtility(hDlg)) util->m_hAnimSettings = nullptr;
        return TRUE;
    }
    return FALSE;
}

}  // namespace max2alamo

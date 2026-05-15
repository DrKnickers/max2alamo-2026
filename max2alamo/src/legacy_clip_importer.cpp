#include "legacy_clip_importer.h"

#include "alamo_format/legacy_clip_scan.h"

#include <Max.h>
#include <maxapi.h>
#include <notify.h>
#include <maxscript/maxscript.h>
#include <maxscript/util/listener.h>

#include <fstream>
#include <string>
#include <vector>

namespace max2alamo {

namespace {

// ---- TCHAR <-> UTF-8 conversions (Win32 wide build only) ------------------

std::wstring to_wide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                  static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        static_cast<int>(s.size()), out.data(), len);
    return out;
}

void SetIntProp(INode* node, const std::string& key, int value)
{
    if (!node) return;
    auto wkey = to_wide(key);
    node->SetUserPropInt(const_cast<TCHAR*>(wkey.c_str()), value);
}

void SetStringProp(INode* node, const std::string& key, const std::string& value)
{
    if (!node) return;
    auto wkey   = to_wide(key);
    auto wvalue = to_wide(value);
    node->SetUserPropString(const_cast<TCHAR*>(wkey.c_str()),
                            const_cast<TCHAR*>(wvalue.c_str()));
}

std::string GetStringProp(INode* node, const std::string& key)
{
    if (!node) return {};
    auto wkey = to_wide(key);
    TSTR value;
    if (!node->GetUserPropString(const_cast<TCHAR*>(wkey.c_str()), value)) return {};
    // Convert wide TSTR to UTF-8 for comparisons.
    int len = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                  static_cast<int>(value.Length()),
                                  nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.Length()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

// ---- File slurp ----------------------------------------------------------

bool ReadEntireFile(const TCHAR* path, std::vector<unsigned char>& out)
{
    if (!path || path[0] == 0) return false;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(out.data()), size)) return false;
    return true;
}

// ---- Diagnostic listener output ------------------------------------------

void ListenerPrintf(const TCHAR* fmt, ...)
{
    if (!the_listener) return;
    va_list args;
    va_start(args, fmt);
    TCHAR buf[512];
    _vsntprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    the_listener->edit_stream->puts(buf);
    the_listener->edit_stream->flush();
}

}  // namespace

int ImportLegacyClipsFromFile(const TCHAR* file_path,
                              INode* root_node,
                              Interface* ip)
{
    if (!root_node) return 0;

    // Modern-wins precedence: if rootNode already has multi-clip
    // authoring, leave it alone. The user (or a prior import pass)
    // has already populated the modern convention.
    if (!GetStringProp(root_node, "Alamo_Anim_Clips").empty()) return 0;

    std::vector<unsigned char> bytes;
    if (!ReadEntireFile(file_path, bytes)) return 0;

    auto records = alamo_format::scan_legacy_clip_records(
        bytes.data(), bytes.size());
    if (records.empty()) return 0;

    // Build the canonical pipe-delimited clip list. Records are in
    // discovery order = file order = the legacy plugin's authored order.
    std::string clip_list;
    int max_end = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (i > 0) clip_list += '|';
        clip_list += records[i].name;
        if (records[i].end_frame > max_end) max_end = records[i].end_frame;
    }

    SetStringProp(root_node, "Alamo_Anim_Clips", clip_list);

    for (const auto& r : records) {
        SetIntProp(root_node, "Alamo_Anim_" + r.name + "_Start", r.start_frame);
        SetIntProp(root_node, "Alamo_Anim_" + r.name + "_End",   r.end_frame);
    }

    // Auto-extend Max's animationRange so the imported keyframes
    // (which live at timeline positions [1 .. max_end]) are within
    // bounds. Don't shrink an existing larger range -- there could be
    // non-Alamo authoring out past the clip ranges that we shouldn't
    // truncate. Frame 0 stays present (legacy convention starts clips
    // at frame 1, matching Mike Lankamp's importer).
    if (ip && max_end > 0) {
        const int tpf = ::GetTicksPerFrame();
        const Interval cur = ip->GetAnimRange();
        const int desired_end_ticks = max_end * tpf;
        if (cur.End() < desired_end_ticks) {
            Interval iv(cur.Start(), desired_end_ticks);
            ip->SetAnimRange(iv);
            BroadcastNotification(NOTIFY_TIMERANGE_CHANGE);
        }
    }

    return static_cast<int>(records.size());
}

void MaybeImportLegacyClipsFromCurrentScene(Interface* ip)
{
    if (!ip) return;
    INode* root = ip->GetRootNode();
    if (!root) return;

    const TCHAR* path = ip->GetCurFilePath();
    if (!path || path[0] == 0) return;  // untitled / unsaved scene

    const int n = ImportLegacyClipsFromFile(path, root, ip);
    if (n > 0) {
        ListenerPrintf(
            _T("max2alamo: imported %d legacy animation clip(s) ")
            _T("from %s\n"), n, path);
    }
}

// Standalone notification handler -- runs the legacy clip importer on
// every NOTIFY_FILE_POST_OPEN regardless of UI state. The void* param
// is unused (Max's RegisterNotification API requires a callback of
// shape `void(*)(void*, NotifyInfo*)`; we pass nullptr at registration).
static void OnFilePostOpenStandalone(void* /*unused*/, NotifyInfo* /*info*/)
{
    MaybeImportLegacyClipsFromCurrentScene(GetCOREInterface());
}

void RegisterLegacyClipImportNotifications()
{
    RegisterNotification(OnFilePostOpenStandalone, nullptr,
                         NOTIFY_FILE_POST_OPEN);
}

void UnregisterLegacyClipImportNotifications()
{
    UnRegisterNotification(OnFilePostOpenStandalone, nullptr,
                           NOTIFY_FILE_POST_OPEN);
}

}  // namespace max2alamo

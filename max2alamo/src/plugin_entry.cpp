// max2alamo plugin DLL entry points.
//
// 3ds Max plugins are DLLs (with the .dle extension) that export a small
// fixed set of C-style symbols. The Max plugin loader calls them at startup
// to discover what classes the DLL provides.
//
// Reference: maxsdk/samples/import_export/3dsexp.cpp:202-225 in the
// 3ds Max 2026 SDK.

#include "alo_export.h"
#include "alamo_utility.h"
#include "alamo_proxy_helper.h"
#include "legacy_clip_importer.h"

#include <Max.h>
#include <iparamb2.h>
#include <plugapi.h>

namespace {

// One ClassDesc per plugin class we expose. Phase 3 ships only the
// .alo SceneExport; .ala animation export gets its own ClassDesc when
// Phase 8 lands.
class AloExportClassDesc : public ClassDesc {
public:
    int          IsPublic() override          { return TRUE; }
    void*        Create(BOOL /*loading*/) override { return new max2alamo::AloExport(); }
    const TCHAR* ClassName() override         { return _T("Alamo Object"); }
    const TCHAR* NonLocalizedClassName() override { return _T("Alamo Object"); }
    SClass_ID    SuperClassID() override      { return SCENE_EXPORT_CLASS_ID; }
    Class_ID     ClassID() override
    {
        // Stable, randomly chosen Class_ID. Once committed and shipped this
        // value MUST NOT change -- Max persists it in scenes that reference
        // the plugin. Generated via genclassid (or any 64-bit random source).
        return Class_ID(0x6ed3a4f1, 0x2b9c7d05);
    }
    const TCHAR* Category() override          { return _T("Scene Export"); }
};

AloExportClassDesc g_alo_export_desc;

}  // namespace

// ---- DLL entry points exported via max2alamo.def ---------------------------

extern "C" __declspec(dllexport) const TCHAR* LibDescription()
{
    return _T("Alamo (.alo / .ala) exporter for 3ds Max 2026");
}

extern "C" __declspec(dllexport) int LibNumberClasses()
{
    // [0] AloExport (SceneExport)
    // [1] AlamoUtility (Utilities command panel)
    // [2] AlamoProxyHelper (Create panel > Helpers > Standard)
    // [3] LegacyAlamoProxyHelper (Phase 10d: hidden ClassDesc for the
    //     legacy Petroglyph plugin's Class_ID, so .max files saved
    //     with the Max 9-era max2alamo.dle load with real Alamo_Proxy
    //     instances instead of Missing_Helper placeholders)
    return 4;
}

extern "C" __declspec(dllexport) ClassDesc* LibClassDesc(int i)
{
    switch (i) {
        case 0:  return &g_alo_export_desc;
        case 1:  return max2alamo::GetAlamoUtilityClassDesc();
        case 2:  return max2alamo::GetAlamoProxyHelperClassDesc();
        case 3:  return max2alamo::GetLegacyAlamoProxyHelperClassDesc();
        default: return nullptr;
    }
}

extern "C" __declspec(dllexport) ULONG LibVersion()
{
    return VERSION_3DSMAX;
}

// Tells Max whether the DLL is safe to defer-load. Phase 11c needs to
// be in memory at Max startup so its NOTIFY_FILE_POST_OPEN handler is
// registered BEFORE the user opens a legacy `.max` file -- otherwise
// the importer never fires and legacy animation clips silently fail to
// translate. Returning 0 forces eager load at Max startup. The cost is
// a few-hundred-KB resident memory bump for users who don't author
// Alamo content; the alternative is silent breakage of the legacy
// clip-import workflow, which is worse.
extern "C" __declspec(dllexport) ULONG CanAutoDefer()
{
    return 0;
}

// LibInitialize runs once when Max loads this DLL (at startup with
// CanAutoDefer=0 above). This is the right place to register
// process-lifetime notification callbacks.
extern "C" __declspec(dllexport) int LibInitialize()
{
    max2alamo::RegisterLegacyClipImportNotifications();
    return TRUE;
}

// LibShutdown runs once at Max exit, paired with LibInitialize.
extern "C" __declspec(dllexport) int LibShutdown()
{
    max2alamo::UnregisterLegacyClipImportNotifications();
    return TRUE;
}

// Standard Win32 DLL entry. Max wants HINSTANCE captured for resource
// loading. Even though Phase 3 doesn't use string resources yet, capturing
// hInstance early is the convention every Max plugin follows so that any
// later resource access works without surprises.
HINSTANCE g_h_instance = nullptr;

BOOL WINAPI DllMain(HINSTANCE h_inst, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_h_instance = h_inst;
        DisableThreadLibraryCalls(h_inst);
    }
    return TRUE;
}

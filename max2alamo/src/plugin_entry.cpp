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
    return 3;
}

extern "C" __declspec(dllexport) ClassDesc* LibClassDesc(int i)
{
    switch (i) {
        case 0:  return &g_alo_export_desc;
        case 1:  return max2alamo::GetAlamoUtilityClassDesc();
        case 2:  return max2alamo::GetAlamoProxyHelperClassDesc();
        default: return nullptr;
    }
}

extern "C" __declspec(dllexport) ULONG LibVersion()
{
    return VERSION_3DSMAX;
}

// Tells Max it's safe to defer loading this DLL until first use, instead
// of pulling it in at startup. Both class types we expose (SceneExport,
// Utility) qualify because neither registers controllers or persistent
// scene data at init -- the Utility's user-prop reads/writes happen
// lazily when the user opens the rollout.
extern "C" __declspec(dllexport) ULONG CanAutoDefer()
{
    return 1;
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

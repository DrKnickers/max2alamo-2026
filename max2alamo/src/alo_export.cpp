#include "alo_export.h"

#include <Max.h>
#include <maxapi.h>

namespace max2alamo {

int AloExport::ExtCount()
{
    return 1;
}

const TCHAR* AloExport::Ext(int n)
{
    switch (n) {
        case 0:  return _T("ALO");
        default: return _T("");
    }
}

const TCHAR* AloExport::LongDesc()
{
    return _T("Alamo Object File (Empire at War / Forces of Corruption)");
}

const TCHAR* AloExport::ShortDesc()
{
    return _T("Alamo Object");
}

const TCHAR* AloExport::AuthorName()
{
    return _T("max2alamo-2026 contributors");
}

const TCHAR* AloExport::CopyrightMessage()
{
    return _T("MIT License -- see LICENSE in https://github.com/DrKnickers/max2alamo-2026");
}

const TCHAR* AloExport::OtherMessage1()  { return _T(""); }
const TCHAR* AloExport::OtherMessage2()  { return _T(""); }

unsigned int AloExport::Version()
{
    return 1;  // version * 100 in some Max conventions; stub for now.
}

void AloExport::ShowAbout(HWND hWnd)
{
    MessageBox(
        hWnd,
        _T("max2alamo-2026 (Phase 3 scaffold)\n\n")
        _T("Exports 3ds Max scenes to Petroglyph .alo / .ala\n")
        _T("for Star Wars: Empire at War and Forces of Corruption.\n\n")
        _T("This is a pre-release scaffold. Export functionality\n")
        _T("lands in subsequent phases."),
        _T("About max2alamo"),
        MB_OK | MB_ICONINFORMATION);
}

int AloExport::DoExport(const TCHAR*  /*name*/,
                        ExpInterface* /*ei*/,
                        Interface*    i,
                        BOOL          suppress_prompts,
                        DWORD         /*options*/)
{
    // Phase 3 stub: confirms the plugin loads, the menu entry is registered,
    // and the export pipeline plumbing reaches our code. Actual scene
    // traversal and writing arrive in Phase 4.
    if (!suppress_prompts && i) {
        MessageBox(
            i->GetMAXHWnd(),
            _T("max2alamo Phase 3 scaffold reached DoExport().\n\n")
            _T("This stub does not yet write a file. Geometry export\n")
            _T("lands in Phase 4."),
            _T("max2alamo"),
            MB_OK | MB_ICONINFORMATION);
    }
    // Returning IMPEXP_FAIL signals a non-fatal cancel-equivalent; Max will
    // not produce a partial output file, which is what we want for the stub.
    return IMPEXP_FAIL;
}

}  // namespace max2alamo

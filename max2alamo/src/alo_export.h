#pragma once

// SceneExport-derived class that registers the Alamo .alo writer with the
// 3ds Max File -> Export menu. Phase 3 stubs DoExport(); Phases 4-7 wire
// in scene traversal, geometry, materials, and connections.

#include <impexp.h>

namespace max2alamo {

class AloExport : public SceneExport {
public:
    AloExport()  = default;
    ~AloExport() override = default;

    int          ExtCount() override;
    const TCHAR* Ext(int n) override;
    const TCHAR* LongDesc() override;
    const TCHAR* ShortDesc() override;
    const TCHAR* AuthorName() override;
    const TCHAR* CopyrightMessage() override;
    const TCHAR* OtherMessage1() override;
    const TCHAR* OtherMessage2() override;
    unsigned int Version() override;
    void         ShowAbout(HWND hWnd) override;

    int  DoExport(const TCHAR* name,
                  ExpInterface* ei,
                  Interface*    i,
                  BOOL          suppress_prompts = FALSE,
                  DWORD         options          = 0) override;
};

}  // namespace max2alamo

#pragma once

// Alamo Utility command-panel plugin.
//
// Faithful clone of the legacy Petroglyph max2alamo Utility UI: a
// three-rollout panel (Node Export Options, Quick Selection Utility,
// Animation Settings) that appears under the command panel's
// Utilities tab > More... > Alamo Utility.
//
// Authoring side of the Alamo_* node user-property family. The
// walker reads the same property names at export time -- the panel
// just gives the user a checkboxes-and-radio-buttons UI rather than
// requiring them to edit the Object Properties > User Defined text
// box by hand.

#include <Max.h>
#include <iparamb2.h>
#include <utilapi.h>
#include <plugapi.h>

namespace max2alamo {

// Stable Class_IDs (must never change after first ship — Max persists
// references in .max scenes). Generated with genclassid; second-part
// chosen distinct from the SceneExport's (0x6ed3a4f1, 0x2b9c7d05).
inline constexpr Class_ID kUtilityClassID(0x6ed3a4f1, 0x4f51ab63);

class AlamoUtility : public UtilityObj {
public:
    AlamoUtility();
    ~AlamoUtility() override = default;

    void DeleteThis() override { /* singleton-instance; owned by ClassDesc */ }

    void BeginEditParams(Interface* ip, IUtil* iu) override;
    void EndEditParams(Interface* ip, IUtil* iu) override;
    void SelectionSetChanged(Interface* ip, IUtil* iu) override;

    // Refresh the Node Export Options rollout's checkboxes/radios to
    // reflect the user properties of the currently-selected INode.
    // Called on selection change and after every property write so the
    // visible state stays in sync with the underlying scene.
    void RefreshNodeOptions();

    // Window handles for each rollout, held while edit is active.
    HWND m_hNodeOptions = nullptr;
    HWND m_hQuickSelect = nullptr;
    HWND m_hAnimSettings = nullptr;

    Interface* m_ip = nullptr;
};

// Singleton ClassDesc surfaced via LibClassDesc.
ClassDesc* GetAlamoUtilityClassDesc();

}  // namespace max2alamo

#include "alamo_utility.h"
#include "alamo_proxy_helper.h"  // kAlamoProxyClassID, for Hidden/AltDec ungate

#include "../resources/utility_resource.h"

#include <Max.h>
#include <maxapi.h>
#include <iparamb2.h>
#include <object.h>      // INode::GetObjectRef()
#include <utilapi.h>
#include <plugapi.h>
#include <custcont.h>   // ISpinnerControl, GetISpinner, SetupIntSpinner

#include <string>

// HINSTANCE captured by DllMain in plugin_entry.cpp.
extern HINSTANCE g_h_instance;

namespace max2alamo {

namespace {

// ---- Alamo_* user-property keys (legacy convention) -----------------------

constexpr const TCHAR* kPropExportTransform     = _T("Alamo_Export_Transform");
constexpr const TCHAR* kPropIsExtraBone         = _T("Alamo_Is_Extra_Bone");
constexpr const TCHAR* kPropBillboardMode       = _T("Alamo_Billboard_Mode");
constexpr const TCHAR* kPropExportGeometry      = _T("Alamo_Export_Geometry");
constexpr const TCHAR* kPropCollisionEnabled    = _T("Alamo_Collision_Enabled");
constexpr const TCHAR* kPropGeometryHidden      = _T("Alamo_Geometry_Hidden");
constexpr const TCHAR* kPropAltDecStayHidden    = _T("Alamo_Alt_Decrease_Stay_Hidden");
constexpr const TCHAR* kPropLOD                 = _T("Alamo_LOD");
constexpr const TCHAR* kPropAlt                 = _T("Alamo_Alt");

// Billboard radio-button ID -> stored int value mapping. Order matches
// the legacy Petroglyph plugin's enum:
//   0 = Disable, 1 = Parallel, 2 = Face, 3 = ZAxis View,
//   4 = ZAxis Light, 5 = ZAxis Wind, 6 = Sunlight Glow, 7 = Sun
const int kBillboardRadioIds[8] = {
    IDC_BILLBOARD_DISABLE,
    IDC_BILLBOARD_PARALLEL,
    IDC_BILLBOARD_FACE,
    IDC_BILLBOARD_ZAXIS_VIEW,
    IDC_BILLBOARD_ZAXIS_LIGHT,
    IDC_BILLBOARD_ZAXIS_WIND,
    IDC_BILLBOARD_SUNLIGHT_GLOW,
    IDC_BILLBOARD_SUN,
};

// ---- User-property helpers ------------------------------------------------

bool ReadBoolProp(INode* node, const TCHAR* key, bool dflt = false)
{
    if (!node) return dflt;
    BOOL v = dflt ? TRUE : FALSE;
    if (node->GetUserPropBool(const_cast<TCHAR*>(key), v)) return v != FALSE;
    return dflt;
}

void WriteBoolProp(INode* node, const TCHAR* key, bool value)
{
    if (!node) return;
    node->SetUserPropBool(const_cast<TCHAR*>(key), value ? TRUE : FALSE);
}

int ReadIntProp(INode* node, const TCHAR* key, int dflt = 0)
{
    if (!node) return dflt;
    int v = dflt;
    if (node->GetUserPropInt(const_cast<TCHAR*>(key), v)) return v;
    return dflt;
}

void WriteIntProp(INode* node, const TCHAR* key, int value)
{
    if (!node) return;
    node->SetUserPropInt(const_cast<TCHAR*>(key), value);
}

// First selected INode in the scene, or nullptr.
INode* GetSingleSelectedNode(Interface* ip)
{
    if (!ip) return nullptr;
    if (ip->GetSelNodeCount() < 1) return nullptr;
    return ip->GetSelNode(0);
}

// True when the node's underlying Object* is our Alamo_Proxy helper
// class. Used to ungate the Hidden / Alt Dec Stay Hidden checkboxes
// for proxy nodes: legacy PG plugin let users edit those flags
// directly via the Utility panel (the gating on Export Geometry was
// a visual grouping for meshes; the flags themselves apply to any
// exportable node, particularly proxies).
bool IsAlamoProxyNode(INode* node)
{
    if (!node) return false;
    Object* obj = node->GetObjectRef();
    if (!obj) return false;
    return obj->ClassID() == kAlamoProxyClassID;
}

// ---- Dialog procs ---------------------------------------------------------

// Pull the AlamoUtility* this dialog was constructed for. Stored in
// GWLP_USERDATA on WM_INITDIALOG; nullptr before that fires.
AlamoUtility* GetUtility(HWND hDlg)
{
    return reinterpret_cast<AlamoUtility*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
}

// Helper: enable / disable a control by ID.
void EnableCtrl(HWND hDlg, int id, bool enable)
{
    HWND h = GetDlgItem(hDlg, id);
    if (h) EnableWindow(h, enable ? TRUE : FALSE);
}

// Node Export Options: load all controls from the selected node's
// user properties; gray out everything if no node (or multiple) selected.
void RefreshNodeOptionsState(AlamoUtility* util)
{
    if (!util || !util->m_hNodeOptions) return;
    HWND h = util->m_hNodeOptions;

    INode* node = (util->m_ip && util->m_ip->GetSelNodeCount() == 1)
                  ? util->m_ip->GetSelNode(0)
                  : nullptr;

    const bool have = (node != nullptr);
    const bool exportXfm = have && ReadBoolProp(node, kPropExportTransform);
    const bool exportGeo = have && ReadBoolProp(node, kPropExportGeometry);
    const int  bbMode    = have ? ReadIntProp(node, kPropBillboardMode, 0) : 0;
    // Alamo_Proxy helpers expose Hidden / Alt Dec Stay Hidden directly
    // (not gated on Export Geometry, which is mesh-specific). The
    // legacy PG plugin's "Export Geometry" gating was a visual group
    // for the mesh-only Enable Collision sub-toggle; the hidden /
    // alt-dec-stay-hidden flags apply to any exportable node, and
    // proxies are the dominant non-mesh user of them.
    const bool isProxy = have && IsAlamoProxyNode(node);

    // Master toggles (always editable when a node is selected)
    EnableCtrl(h, IDC_NODE_EXPORT_TRANSFORM, have);
    EnableCtrl(h, IDC_NODE_EXPORT_GEOMETRY,  have);

    // Dependent toggles: only meaningful when their parent is checked.
    EnableCtrl(h, IDC_NODE_IS_EXTRA_BONE,      have && exportXfm);
    EnableCtrl(h, IDC_BILLBOARD_GROUP,         have && exportXfm);
    EnableCtrl(h, IDC_BILLBOARD_HELP,          have && exportXfm);
    for (int id : kBillboardRadioIds) EnableCtrl(h, id, have && exportXfm);

    // Enable Collision: mesh-only (requires Export Geometry checked).
    EnableCtrl(h, IDC_NODE_ENABLE_COLLISION,   have && exportGeo);
    // Hidden / Alt Dec Stay Hidden: editable when Export Geometry is
    // checked (mesh path) OR when the selected node is an Alamo_Proxy
    // (proxy path -- the legacy plugin let users edit these flags
    // directly for proxies without needing the mesh-side gate).
    EnableCtrl(h, IDC_NODE_HIDDEN,             have && (exportGeo || isProxy));
    EnableCtrl(h, IDC_NODE_ALT_DEC_STAY_HIDDEN, have && (exportGeo || isProxy));

    // Checkbox / radio state
    CheckDlgButton(h, IDC_NODE_EXPORT_TRANSFORM, exportXfm ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_NODE_IS_EXTRA_BONE,
                   (have && ReadBoolProp(node, kPropIsExtraBone)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_NODE_EXPORT_GEOMETRY, exportGeo ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_NODE_ENABLE_COLLISION,
                   (have && ReadBoolProp(node, kPropCollisionEnabled)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_NODE_HIDDEN,
                   (have && ReadBoolProp(node, kPropGeometryHidden)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_NODE_ALT_DEC_STAY_HIDDEN,
                   (have && ReadBoolProp(node, kPropAltDecStayHidden)) ? BST_CHECKED : BST_UNCHECKED);

    const int safeMode = (bbMode >= 0 && bbMode < 8) ? bbMode : 0;
    for (int i = 0; i < 8; ++i) {
        CheckDlgButton(h, kBillboardRadioIds[i],
                       (i == safeMode) ? BST_CHECKED : BST_UNCHECKED);
    }
}

// Returns the billboard-mode int (0..7) that's currently selected in
// the radio group, or -1 if nothing is checked.
int GetBillboardSelection(HWND hDlg)
{
    for (int i = 0; i < 8; ++i) {
        if (IsDlgButtonChecked(hDlg, kBillboardRadioIds[i]) == BST_CHECKED) {
            return i;
        }
    }
    return -1;
}

INT_PTR CALLBACK NodeOptionsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        if (auto* util = reinterpret_cast<AlamoUtility*>(lParam)) {
            util->m_hNodeOptions = hDlg;
        }
        RefreshNodeOptionsState(reinterpret_cast<AlamoUtility*>(lParam));
        return TRUE;

    case WM_COMMAND: {
        auto* util = GetUtility(hDlg);
        if (!util || !util->m_ip) return FALSE;
        INode* node = GetSingleSelectedNode(util->m_ip);
        if (!node && LOWORD(wParam) != IDC_BILLBOARD_HELP) return FALSE;

        const int id = LOWORD(wParam);
        const WORD code = HIWORD(wParam);

        if (code == BN_CLICKED) {
            switch (id) {
            case IDC_NODE_EXPORT_TRANSFORM:
                WriteBoolProp(node, kPropExportTransform,
                              IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
                RefreshNodeOptionsState(util);
                return TRUE;
            case IDC_NODE_IS_EXTRA_BONE:
                WriteBoolProp(node, kPropIsExtraBone,
                              IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
                return TRUE;
            case IDC_NODE_EXPORT_GEOMETRY:
                WriteBoolProp(node, kPropExportGeometry,
                              IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
                RefreshNodeOptionsState(util);
                return TRUE;
            case IDC_NODE_ENABLE_COLLISION:
                WriteBoolProp(node, kPropCollisionEnabled,
                              IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
                return TRUE;
            case IDC_NODE_HIDDEN:
                WriteBoolProp(node, kPropGeometryHidden,
                              IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
                return TRUE;
            case IDC_NODE_ALT_DEC_STAY_HIDDEN:
                WriteBoolProp(node, kPropAltDecStayHidden,
                              IsDlgButtonChecked(hDlg, id) == BST_CHECKED);
                return TRUE;
            case IDC_BILLBOARD_DISABLE:
            case IDC_BILLBOARD_PARALLEL:
            case IDC_BILLBOARD_FACE:
            case IDC_BILLBOARD_ZAXIS_VIEW:
            case IDC_BILLBOARD_ZAXIS_LIGHT:
            case IDC_BILLBOARD_ZAXIS_WIND:
            case IDC_BILLBOARD_SUNLIGHT_GLOW:
            case IDC_BILLBOARD_SUN: {
                const int mode = GetBillboardSelection(hDlg);
                if (mode >= 0) WriteIntProp(node, kPropBillboardMode, mode);
                return TRUE;
            }
            case IDC_BILLBOARD_HELP:
                MessageBox(hDlg,
                    _T("Billboarding Options\n\n")
                    _T("Disable        — no billboarding\n")
                    _T("Parallel       — face the camera, ignoring roll\n")
                    _T("Face           — face the camera fully\n")
                    _T("ZAxis View     — rotate around local Z to face camera\n")
                    _T("ZAxis Light    — rotate around local Z toward main light\n")
                    _T("ZAxis Wind     — rotate around local Z toward wind\n")
                    _T("Sunlight Glow  — solar-glow lens-flare billboard\n")
                    _T("Sun            — Sun-disc billboard"),
                    _T("Billboarding Options"), MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
        }
        return FALSE;
    }

    case WM_DESTROY:
        if (auto* util = GetUtility(hDlg)) util->m_hNodeOptions = nullptr;
        return TRUE;
    }
    return FALSE;
}

// Quick Selection Utility: selects all scene nodes whose user
// property matches the rollout's criteria. Replaces the current
// selection (matches the legacy plugin's behaviour).
void SelectNodesWithProp(Interface* ip, const TCHAR* propKey, bool wantTrue)
{
    if (!ip) return;
    INode* root = ip->GetRootNode();
    if (!root) return;

    INodeTab matches;
    // Depth-first walk via children.
    struct Walk {
        static void recurse(INode* n, const TCHAR* key, bool wantTrue, INodeTab& out) {
            if (!n) return;
            BOOL v = FALSE;
            const bool present = n->GetUserPropBool(const_cast<TCHAR*>(key), v) != FALSE;
            if (present && (wantTrue ? (v != FALSE) : (v == FALSE))) {
                INode* p = n;
                out.Append(1, &p);
            }
            for (int i = 0; i < n->NumberOfChildren(); ++i) {
                recurse(n->GetChildNode(i), key, wantTrue, out);
            }
        }
    };
    for (int i = 0; i < root->NumberOfChildren(); ++i) {
        Walk::recurse(root->GetChildNode(i), propKey, wantTrue, matches);
    }

    theHold.Begin();
    ip->ClearNodeSelection(FALSE);
    if (matches.Count() > 0) {
        ip->SelectNodeTab(matches, TRUE, TRUE);
    }
    theHold.Accept(_T("Alamo: Quick Select"));
    ip->RedrawViews(ip->GetTime());
}

void SelectNodesWithIntProp(Interface* ip, const TCHAR* propKey, int wantValue)
{
    if (!ip) return;
    INode* root = ip->GetRootNode();
    if (!root) return;

    INodeTab matches;
    struct Walk {
        static void recurse(INode* n, const TCHAR* key, int wantValue, INodeTab& out) {
            if (!n) return;
            int v = 0;
            if (n->GetUserPropInt(const_cast<TCHAR*>(key), v) && v == wantValue) {
                INode* p = n;
                out.Append(1, &p);
            }
            for (int i = 0; i < n->NumberOfChildren(); ++i) {
                recurse(n->GetChildNode(i), key, wantValue, out);
            }
        }
    };
    for (int i = 0; i < root->NumberOfChildren(); ++i) {
        Walk::recurse(root->GetChildNode(i), propKey, wantValue, matches);
    }

    theHold.Begin();
    ip->ClearNodeSelection(FALSE);
    if (matches.Count() > 0) {
        ip->SelectNodeTab(matches, TRUE, TRUE);
    }
    theHold.Accept(_T("Alamo: Quick Select by int"));
    ip->RedrawViews(ip->GetTime());
}

INT_PTR CALLBACK QuickSelectionDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        if (auto* util = reinterpret_cast<AlamoUtility*>(lParam)) {
            util->m_hQuickSelect = hDlg;
        }
        if (ISpinnerControl* s = SetupIntSpinner(hDlg, IDC_QS_LOD_SPIN, IDC_QS_LOD_EDIT,
                                                 0, 16, 0)) {
            ReleaseISpinner(s);
        }
        if (ISpinnerControl* s = SetupIntSpinner(hDlg, IDC_QS_ALT_SPIN, IDC_QS_ALT_EDIT,
                                                 0, 16, 0)) {
            ReleaseISpinner(s);
        }
        return TRUE;
    }

    case WM_COMMAND: {
        auto* util = GetUtility(hDlg);
        if (!util || !util->m_ip) return FALSE;
        const int id = LOWORD(wParam);
        if (HIWORD(wParam) == BN_CLICKED) {
            switch (id) {
            case IDC_QS_EXPORT_TRANSFORM:
                SelectNodesWithProp(util->m_ip, kPropExportTransform, true);
                return TRUE;
            case IDC_QS_EXPORT_GEOMETRY:
                SelectNodesWithProp(util->m_ip, kPropExportGeometry, true);
                return TRUE;
            case IDC_QS_ENABLE_COLLISION:
                SelectNodesWithProp(util->m_ip, kPropCollisionEnabled, true);
                return TRUE;
            case IDC_QS_LOD_HELP:
                MessageBox(hDlg,
                    _T("LOD (level-of-detail) index.\n\n")
                    _T("Click to select every node in the scene whose ")
                    _T("Alamo_LOD user property equals the spinner value. ")
                    _T("LOD 0 is the highest-detail variant; engines pick ")
                    _T("a lower-LOD copy of the same mesh family when ")
                    _T("distance / fidelity heuristics call for it."),
                    _T("Alamo LOD"), MB_OK | MB_ICONINFORMATION);
                return TRUE;
            case IDC_QS_ALT_HELP:
                MessageBox(hDlg,
                    _T("Alt (alternative) variant index.\n\n")
                    _T("Click to select every node whose Alamo_Alt user ")
                    _T("property equals the spinner value. Alts are used ")
                    _T("for damaged / variant geometry on the same model ")
                    _T("(e.g. a ship with intact / damaged / destroyed ")
                    _T("variants tagged Alt=0/1/2)."),
                    _T("Alamo Alt"), MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
        }
        return FALSE;
    }

    // CC_SPINNER_BUTTONUP fires when the user finishes a spinner edit.
    // We use it as "user committed a new LOD/Alt value -> apply as
    // selection filter."
    case CC_SPINNER_BUTTONUP: {
        auto* util = GetUtility(hDlg);
        if (!util || !util->m_ip) return FALSE;
        const int id = LOWORD(wParam);
        if (id == IDC_QS_LOD_SPIN) {
            if (ISpinnerControl* s = GetISpinner(GetDlgItem(hDlg, IDC_QS_LOD_SPIN))) {
                SelectNodesWithIntProp(util->m_ip, kPropLOD, s->GetIVal());
                ReleaseISpinner(s);
            }
            return TRUE;
        }
        if (id == IDC_QS_ALT_SPIN) {
            if (ISpinnerControl* s = GetISpinner(GetDlgItem(hDlg, IDC_QS_ALT_SPIN))) {
                SelectNodesWithIntProp(util->m_ip, kPropAlt, s->GetIVal());
                ReleaseISpinner(s);
            }
            return TRUE;
        }
        return FALSE;
    }

    case WM_DESTROY:
        if (auto* util = GetUtility(hDlg)) util->m_hQuickSelect = nullptr;
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK AnimationSettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        if (auto* util = reinterpret_cast<AlamoUtility*>(lParam)) {
            util->m_hAnimSettings = hDlg;
        }
        // Seed the combo with the legacy "no clip authored" sentinel.
        // The real clip store lands with the Phase 8 .ala writer.
        SendDlgItemMessage(hDlg, IDC_ANIM_NAME_COMBO, CB_RESETCONTENT, 0, 0);
        SendDlgItemMessage(hDlg, IDC_ANIM_NAME_COMBO, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(_T("-- none --")));
        SendDlgItemMessage(hDlg, IDC_ANIM_NAME_COMBO, CB_SETCURSEL, 0, 0);

        if (ISpinnerControl* s = SetupIntSpinner(hDlg, IDC_ANIM_START_SPIN,
                                                 IDC_ANIM_START_EDIT, 0, 10000, 0)) {
            ReleaseISpinner(s);
        }
        if (ISpinnerControl* s = SetupIntSpinner(hDlg, IDC_ANIM_END_SPIN,
                                                 IDC_ANIM_END_EDIT, 0, 10000, 0)) {
            ReleaseISpinner(s);
        }
        // The clip backend is Phase 8 work; everything past visual
        // setup is intentionally inert here.
        EnableCtrl(hDlg, IDC_ANIM_NAME_COMBO,       FALSE);
        EnableCtrl(hDlg, IDC_ANIM_START_EDIT,       FALSE);
        EnableCtrl(hDlg, IDC_ANIM_START_SPIN,       FALSE);
        EnableCtrl(hDlg, IDC_ANIM_END_EDIT,         FALSE);
        EnableCtrl(hDlg, IDC_ANIM_END_SPIN,         FALSE);
        EnableCtrl(hDlg, IDC_ANIM_PREV,             FALSE);
        EnableCtrl(hDlg, IDC_ANIM_ADD,              FALSE);
        EnableCtrl(hDlg, IDC_ANIM_DEL,              FALSE);
        EnableCtrl(hDlg, IDC_ANIM_NEXT,             FALSE);
        EnableCtrl(hDlg, IDC_ANIM_DISPLAY_CURRENT,  FALSE);
        EnableCtrl(hDlg, IDC_ANIM_DISPLAY_ALL,      FALSE);
        return TRUE;
    }
    case WM_DESTROY:
        if (auto* util = GetUtility(hDlg)) util->m_hAnimSettings = nullptr;
        return TRUE;
    }
    return FALSE;
}

// ---- ClassDesc ------------------------------------------------------------

class AlamoUtilityClassDesc : public ClassDesc {
public:
    int          IsPublic() override          { return TRUE; }
    void*        Create(BOOL /*loading*/) override
    {
        // Singleton: Max only ever instantiates one Utility instance,
        // and we own the lifetime here (DeleteThis is a no-op).
        static AlamoUtility instance;
        return &instance;
    }
    const TCHAR* ClassName() override         { return _T("Alamo Utility"); }
    const TCHAR* NonLocalizedClassName() override { return _T("Alamo Utility"); }
    SClass_ID    SuperClassID() override      { return UTILITY_CLASS_ID; }
    Class_ID     ClassID() override           { return kUtilityClassID; }
    const TCHAR* Category() override          { return _T("Alamo"); }
};

AlamoUtilityClassDesc g_alamo_utility_desc;

}  // namespace

ClassDesc* GetAlamoUtilityClassDesc() { return &g_alamo_utility_desc; }

// ---- AlamoUtility ---------------------------------------------------------

AlamoUtility::AlamoUtility() = default;

void AlamoUtility::BeginEditParams(Interface* ip, IUtil* /*iu*/)
{
    m_ip = ip;
    if (!ip) return;
    ip->AddRollupPage(g_h_instance,
                      MAKEINTRESOURCE(IDD_ALAMO_NODE_EXPORT_OPTIONS),
                      NodeOptionsDlgProc,
                      _T("Node Export Options"),
                      reinterpret_cast<LPARAM>(this));
    ip->AddRollupPage(g_h_instance,
                      MAKEINTRESOURCE(IDD_ALAMO_QUICK_SELECTION),
                      QuickSelectionDlgProc,
                      _T("Quick Selection Utility"),
                      reinterpret_cast<LPARAM>(this));
    ip->AddRollupPage(g_h_instance,
                      MAKEINTRESOURCE(IDD_ALAMO_ANIMATION_SETTINGS),
                      AnimationSettingsDlgProc,
                      _T("Animation Settings"),
                      reinterpret_cast<LPARAM>(this));
}

void AlamoUtility::EndEditParams(Interface* ip, IUtil* /*iu*/)
{
    if (!ip) { m_ip = nullptr; return; }
    if (m_hNodeOptions)  ip->DeleteRollupPage(m_hNodeOptions);
    if (m_hQuickSelect)  ip->DeleteRollupPage(m_hQuickSelect);
    if (m_hAnimSettings) ip->DeleteRollupPage(m_hAnimSettings);
    m_hNodeOptions = m_hQuickSelect = m_hAnimSettings = nullptr;
    m_ip = nullptr;
}

void AlamoUtility::SelectionSetChanged(Interface* /*ip*/, IUtil* /*iu*/)
{
    RefreshNodeOptions();
}

void AlamoUtility::RefreshNodeOptions()
{
    RefreshNodeOptionsState(this);
}

}  // namespace max2alamo

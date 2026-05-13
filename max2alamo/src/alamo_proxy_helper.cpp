#include "alamo_proxy_helper.h"

#include <Max.h>
#include <maxapi.h>
#include <object.h>
#include <plugapi.h>
#include <iparamb2.h>

// HINSTANCE captured by DllMain in plugin_entry.cpp.
extern HINSTANCE g_h_instance;

namespace max2alamo {

namespace {

// ---- Geometry of the on-screen marker -------------------------------------

// The Alamo Proxy displays as a small wireframe pyramid pointing up
// along +Z (matches the legacy Max 9 plugin's visual). All sizes are
// world-space units; the marker is small but selectable.
constexpr float kMarkerSize = 10.0f;

}  // namespace

// ---- HelperObject subclass ------------------------------------------------

class AlamoProxyHelper : public HelperObject {
public:
    AlamoProxyHelper() = default;
    ~AlamoProxyHelper() override = default;

    // ----- Animatable / RefTarget core -----------------------------------
    Class_ID  ClassID() override                              { return kAlamoProxyClassID; }
    SClass_ID SuperClassID() override                         { return HELPER_CLASS_ID; }
    void      GetClassName(MSTR& s, bool /*localized*/) const override { s = _T("Alamo_Proxy"); }
    void      DeleteThis() override                           { delete this; }

    int  NumSubs() override                                   { return 0; }
    Animatable* SubAnim(int /*i*/) override                   { return nullptr; }
    int  NumRefs() override                                   { return 0; }
    RefTargetHandle GetReference(int /*i*/) override          { return nullptr; }
    void SetReference(int /*i*/, RefTargetHandle /*rtarg*/) override {}

    RefResult NotifyRefChanged(const Interval& /*interval*/,
                               RefTargetHandle /*hTarget*/,
                               PartID& /*partID*/,
                               RefMessage /*msg*/,
                               BOOL /*propagate*/) override
    { return REF_SUCCEED; }

    RefTargetHandle Clone(RemapDir& remap) override
    {
        auto* clone = new AlamoProxyHelper();
        BaseClone(this, clone, remap);
        return clone;
    }

    // ----- Object identity ------------------------------------------------
    const TCHAR* GetObjectName(bool /*localized*/) const override { return _T("Alamo Proxy"); }
    void InitNodeName(TSTR& s) override { s = _T("Alamo Proxy"); }
    int  IsRenderable() override { return 0; }   // helpers never render
    int  UsesWireColor() override { return 1; }

    // CanConvertToType / ConvertToType: helpers usually accept "anyObject"
    // (which means "I don't convert to a renderable form"). Mirrors the
    // built-in Point Helper and Mike's importer's `Alamo_Proxy` plugin.
    int CanConvertToType(Class_ID obtype) override
    { return (obtype == Class_ID(0, 0)) ? TRUE : FALSE; }

    // ----- Display + hit test --------------------------------------------
    int  Display(TimeValue t, INode* inode, ViewExp* vpt, int flags) override;
    int  HitTest(TimeValue t, INode* inode, int type, int crossing,
                 int flags, IPoint2* p, ViewExp* vpt) override;
    void GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;
    void GetLocalBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box) override;

    // ----- Object base ---------------------------------------------------
    ObjectState Eval(TimeValue /*t*/) override { return ObjectState(this); }
    Interval    ObjectValidity(TimeValue /*t*/) override { return FOREVER; }
    int         DoOwnSelectHilite() override { return 1; }

    CreateMouseCallBack* GetCreateMouseCallBack() override;

    IOResult Save(ISave* /*isave*/) override { return IO_OK; }
    IOResult Load(ILoad* /*iload*/) override { return IO_OK; }

private:
    // Marker is a small wireframe pyramid in object space, pointing +Z.
    static void draw_marker(GraphicsWindow* gw, Matrix3& tm);
};

// ---- Marker drawing -------------------------------------------------------

void AlamoProxyHelper::draw_marker(GraphicsWindow* gw, Matrix3& tm)
{
    if (!gw) return;
    gw->setTransform(tm);

    // 5 verts: 4 base corners + 1 apex.
    const float s = kMarkerSize;
    Point3 v[5] = {
        Point3(-s, -s, 0.f),
        Point3( s, -s, 0.f),
        Point3( s,  s, 0.f),
        Point3(-s,  s, 0.f),
        Point3( 0.f, 0.f, s * 1.5f),  // apex above the base
    };

    // Draw the base square + 4 edges to the apex. polyline takes
    // arrays of points; we draw each edge as a 2-vertex polyline so
    // hit-testing picks up any segment.
    auto line = [&](const Point3& a, const Point3& b) {
        Point3 pts[2] = { a, b };
        gw->polyline(2, pts, nullptr, nullptr, FALSE, nullptr);
    };
    line(v[0], v[1]);
    line(v[1], v[2]);
    line(v[2], v[3]);
    line(v[3], v[0]);
    line(v[0], v[4]);
    line(v[1], v[4]);
    line(v[2], v[4]);
    line(v[3], v[4]);
}

int AlamoProxyHelper::Display(TimeValue t, INode* inode, ViewExp* vpt, int /*flags*/)
{
    if (!vpt || !vpt->IsAlive() || !inode) return 0;

    GraphicsWindow* gw = vpt->getGW();
    if (!gw) return 0;

    Matrix3 tm = inode->GetObjectTM(t);
    DWORD limits = gw->getRndLimits();
    gw->setRndLimits(GW_WIREFRAME | GW_EDGES_ONLY);

    // Wire colour. We just use the node's wireframe colour; Max's
    // viewport handles the selection / frozen highlight tint
    // automatically when the node is in those states (selection-aware
    // colour helpers like GetSelColor live in gfx.lib which we'd
    // rather not pull in for a marker this small).
    Color color(inode->GetWireColor());
    gw->setColor(LINE_COLOR, color);

    draw_marker(gw, tm);

    gw->setRndLimits(limits);
    return 0;
}

int AlamoProxyHelper::HitTest(TimeValue t, INode* inode, int type, int crossing,
                              int /*flags*/, IPoint2* p, ViewExp* vpt)
{
    if (!vpt || !vpt->IsAlive() || !inode || !p) return 0;
    GraphicsWindow* gw = vpt->getGW();
    if (!gw) return 0;

    HitRegion hr;
    MakeHitRegion(hr, type, crossing, /*radius=*/4, p);
    DWORD limits = gw->getRndLimits();
    gw->setRndLimits((limits | GW_PICK) & ~GW_ILLUM);
    gw->setHitRegion(&hr);
    gw->clearHitCode();

    Matrix3 tm = inode->GetObjectTM(t);
    draw_marker(gw, tm);

    const int hit = gw->checkHitCode();
    gw->setRndLimits(limits);
    return hit;
}

void AlamoProxyHelper::GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* /*vpt*/, Box3& box)
{
    if (!inode) { box.Init(); return; }
    Matrix3 tm = inode->GetObjectTM(t);
    Box3 local;
    GetLocalBoundBox(t, inode, nullptr, local);
    box.Init();
    for (int i = 0; i < 8; ++i) box += tm * local[i];
}

void AlamoProxyHelper::GetLocalBoundBox(TimeValue /*t*/, INode* /*inode*/, ViewExp* /*vpt*/, Box3& box)
{
    const float s = kMarkerSize;
    box.Init();
    box += Point3(-s, -s, 0.f);
    box += Point3( s,  s, s * 1.5f);
}

// ---- Create-mouse callback (single-click placement) -----------------------

class AlamoProxyCreateCallback : public CreateMouseCallBack {
public:
    int proc(ViewExp* vpt, int msg, int point, int /*flags*/, IPoint2 m, Matrix3& mat) override
    {
        if (!vpt || !vpt->IsAlive()) return CREATE_ABORT;
        if (msg == MOUSE_POINT) {
            mat.SetTrans(vpt->SnapPoint(m, m, nullptr, SNAP_IN_PLANE));
            // Single click commits creation (point == 0 = first click).
            if (point == 0) return CREATE_STOP;
        } else if (msg == MOUSE_MOVE) {
            mat.SetTrans(vpt->SnapPoint(m, m, nullptr, SNAP_IN_PLANE));
        } else if (msg == MOUSE_ABORT) {
            return CREATE_ABORT;
        }
        return TRUE;
    }
};

static AlamoProxyCreateCallback g_create_cb;

CreateMouseCallBack* AlamoProxyHelper::GetCreateMouseCallBack()
{
    return &g_create_cb;
}

// ---- ClassDesc ------------------------------------------------------------

class AlamoProxyHelperClassDesc : public ClassDesc {
public:
    int          IsPublic() override             { return TRUE; }
    void*        Create(BOOL /*loading*/) override { return new AlamoProxyHelper(); }
    const TCHAR* ClassName() override            { return _T("Alamo Proxy"); }
    const TCHAR* NonLocalizedClassName() override { return _T("Alamo_Proxy"); }
    SClass_ID    SuperClassID() override         { return HELPER_CLASS_ID; }
    Class_ID     ClassID() override              { return kAlamoProxyClassID; }
    const TCHAR* Category() override             { return _T("Standard"); }
    // Internal name -- shows in MAXScript / scripted access. Matches
    // the legacy plugin so existing MAXScript code (Mike's importer
    // at alamo2max.ms:1344, `Alamo_Proxy name:proxy.name`) reaches us.
    const TCHAR* InternalName() override         { return _T("Alamo_Proxy"); }
    HINSTANCE    HInstance() override            { return g_h_instance; }
};

AlamoProxyHelperClassDesc g_alamo_proxy_desc;

ClassDesc* GetAlamoProxyHelperClassDesc()
{
    return &g_alamo_proxy_desc;
}

}  // namespace max2alamo

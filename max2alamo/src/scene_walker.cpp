#include "scene_walker.h"

#include <Max.h>
#include <maxapi.h>
#include <stdmat.h>      // ID_DI = standard-material diffuse slot constant
#include <iparamb2.h>
#include <pbbitmap.h>    // full PBBitmap definition (forward-only via ifnpub)
#include <IDxMaterial.h> // DirectX Shader material interface
#include <AssetManagement/AssetUser.h>

#include <IGame/IGame.h>
#include <IGame/IGameObject.h>
#include <IGame/IGameMaterial.h>
#include <IGame/IConversionManager.h>

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>

namespace max2alamo {

namespace {

// Convert a Max TSTR (wide on Unicode builds) to UTF-8 std::string.
// Plugin builds with /D UNICODE so TCHAR == wchar_t.
std::string to_utf8(const TCHAR* s)
{
    if (!s) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// Strip the directory portion from a file path. Alamo material chunks
// store just the filename (e.g. "tex_box.tga"); the engine resolves
// against its own texture search paths.
std::string basename(const std::string& path)
{
    auto pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// Read a string user-property from a Max INode. Returns the value (UTF-8)
// if the property exists, otherwise empty string.
std::string read_node_user_prop(INode* node, const TCHAR* key)
{
    if (!node || !key) return {};
    MSTR key_mstr(key);
    MSTR value;
    if (!node->GetUserPropString(key_mstr, value)) return {};
    if (value.length() == 0) return {};
    return to_utf8(value.data());
}

// If `mat` is a DirectX Shader material, return its IDxMaterial interface
// (or one of the newer revisions). Returns nullptr for any other material.
IDxMaterial* as_dx_material(Mtl* mat)
{
    if (!mat) return nullptr;
    if (auto* p = static_cast<IDxMaterial*>(mat->GetInterface(IDXMATERIAL_INTERFACE))) {
        return p;
    }
    return nullptr;
}

struct ExtractedMaterial {
    std::string shader_name;   // e.g. "MeshAlpha.fx" (Standard) or the .fx filename (DXMaterial)
    std::string base_texture;  // diffuse / first-non-empty bitmap, basename only
    std::string source_kind;   // diagnostic tag -- "Standard", "DXMaterial", "Multi -> ...",
                               // "UserProp(Alamo_Shader_Name)+Standard", etc.
};

// Node-level user property name. If set on a mesh node, its value
// overrides whatever shader name we'd otherwise pick from the material.
// This is the practical workaround for Max 2026's DirectX Shader
// material being unable to compile EaW's DX9-era HLSL: users put a
// plain Standard material on the mesh for the texture, then add this
// user property to choose the actual Alamo shader.
constexpr const TCHAR* kShaderOverrideKey = _T("Alamo_Shader_Name");

// Inspect a Max material and figure out what shader / texture pair best
// represents it for Phase 4 export. Decision tree:
//   1. DXMaterial -> shader_name = its .fx filename; base_texture = first
//                    non-empty effect bitmap.
//   2. Multi/Sub-Object with SubMaterialCount > 0 -> recurse into sub[0].
//      (Phase 4 single-material assumption; respecting face matIDs is
//      a later phase.)
//   3. Standard material (or anything else) -> shader_name defaults to
//      "MeshAlpha.fx"; base_texture from the diffuse-slot bitmap.
ExtractedMaterial extract_material(IGameMaterial* gmat)
{
    ExtractedMaterial out;
    out.shader_name = "MeshAlpha.fx";
    out.source_kind = "Other";

    if (!gmat) return out;

    // DXMaterial check (first -- a Multi/Sub-Object can't also be DX,
    // and a DX material wraps a Standard material's parameters under
    // the hood so the IGame Standard-material accessors would lie).
    if (Mtl* maxmat = gmat->GetMaxMaterial()) {
        if (IDxMaterial* dx = as_dx_material(maxmat)) {
            out.source_kind = "DXMaterial";
            const MSTR& fn = dx->GetEffectFile().GetFileName();
            if (fn.length() > 0) {
                out.shader_name = basename(to_utf8(fn.data()));
            }
            const int n = dx->GetNumberOfEffectBitmaps();
            for (int i = 0; i < n; ++i) {
                if (PBBitmap* pb = dx->GetEffectBitmap(i)) {
                    const TCHAR* path = pb->bi.Name();
                    if (path && path[0]) {
                        out.base_texture = basename(to_utf8(path));
                        break;
                    }
                }
            }
            return out;
        }
    }

    // Multi/Sub-Object: recurse into first sub.
    if (gmat->GetSubMaterialCount() > 0) {
        if (IGameMaterial* sub = gmat->GetSubMaterial(0)) {
            ExtractedMaterial inner = extract_material(sub);
            // Mark the outer kind as Multi so diagnostics show the path.
            inner.source_kind = "Multi -> " + inner.source_kind;
            return inner;
        }
    }

    // Plain Standard material: scan for a Diffuse-slot bitmap.
    out.source_kind = "Standard";
    const int n = gmat->GetNumberOfTextureMaps();
    for (int i = 0; i < n; ++i) {
        IGameTextureMap* tex = gmat->GetIGameTextureMap(i);
        if (!tex) continue;
        if (tex->GetStdMapSlot() != ID_DI) continue;
        const TCHAR* fn = tex->GetBitmapFileName();
        if (!fn) continue;
        out.base_texture = basename(to_utf8(fn));
        break;
    }
    return out;
}

void update_bbox(alamo_format::ExportMesh& mesh, const Point3& p)
{
    mesh.bbox_min[0] = std::min(mesh.bbox_min[0], p.x);
    mesh.bbox_min[1] = std::min(mesh.bbox_min[1], p.y);
    mesh.bbox_min[2] = std::min(mesh.bbox_min[2], p.z);
    mesh.bbox_max[0] = std::max(mesh.bbox_max[0], p.x);
    mesh.bbox_max[1] = std::max(mesh.bbox_max[1], p.y);
    mesh.bbox_max[2] = std::max(mesh.bbox_max[2], p.z);
}

// Build one ExportMesh from an IGameNode wrapping an IGameMesh. Returns
// std::nullopt-style empty optional via the bool return; mesh is filled
// only on success.
bool build_mesh(IGameNode* node, IGameMesh* gmesh, alamo_format::ExportMesh& out)
{
    out.name = to_utf8(node->GetName());

    const int face_count = gmesh->GetNumberOfFaces();
    if (face_count <= 0) {
        return false;  // skip degenerate / empty meshes
    }

    // Phase 4: single submesh, single material slot. Material extraction
    // handles both Standard and DirectX Shader (DXMaterial) materials,
    // recursing one level into Multi/Sub-Object. Full per-face matID
    // buckets are a later phase.
    ExtractedMaterial em = extract_material(node->GetNodeMaterial());
    // Node-level shader-name override (Alamo_Shader_Name user property).
    // Wins over whatever the material reported -- needed because Max 2026
    // can't compile EaW's DX9 .fx files via DXMaterial, so users author
    // with Standard materials and pick the shader by user property.
    INode* inode = node->GetMaxNode();
    if (std::string ovr = read_node_user_prop(inode, kShaderOverrideKey); !ovr.empty()) {
        em.shader_name = std::move(ovr);
        em.source_kind = "UserProp(Alamo_Shader_Name) + " + em.source_kind;
    }
    alamo_format::ExportSubmesh sub;
    sub.material.shader_name  = std::move(em.shader_name);
    sub.material.base_texture = std::move(em.base_texture);
    sub.vertices.reserve(static_cast<std::size_t>(face_count) * 3u);
    sub.indices.reserve(static_cast<std::size_t>(face_count) * 3u);

    // Initialise bbox to first vertex if available.
    bool bbox_seeded = false;
    out.bbox_min = { std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max() };
    out.bbox_max = { std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest() };

    // Phase 4 emits 3 unique vertices per face (no welding). Welding is a
    // size optimisation that doesn't affect correctness; it can land in a
    // follow-up phase if file sizes become an issue.
    for (int f = 0; f < face_count; ++f) {
        FaceEx* face = gmesh->GetFace(f);
        if (!face) continue;

        for (int corner = 0; corner < 3; ++corner) {
            alamo_format::ExportVertex v;

            const Point3 pos = gmesh->GetVertex(static_cast<int>(face->vert[corner]),
                                                /*ObjectSpace=*/true);
            v.position = { pos.x, pos.y, pos.z };
            update_bbox(out, pos);
            bbox_seeded = true;

            const Point3 nrm = gmesh->GetNormal(static_cast<int>(face->norm[corner]),
                                                /*ObjectSpace=*/true);
            v.normal = { nrm.x, nrm.y, nrm.z };

            // Map channel 1 = standard UV channel in Max.
            const Point2 tex = gmesh->GetTexVertex(static_cast<int>(face->texCoord[corner]));
            // V-flip on write (Alamo / D3D convention vs Max convention).
            v.uv = { tex.x, 1.0f - tex.y };

            sub.vertices.push_back(v);
            sub.indices.push_back(static_cast<std::uint32_t>(sub.vertices.size() - 1u));
        }
    }

    if (!bbox_seeded) {
        out.bbox_min = { 0.f, 0.f, 0.f };
        out.bbox_max = { 0.f, 0.f, 0.f };
    }

    out.submeshes.push_back(std::move(sub));
    out.is_hidden = node->IsNodeHidden() != FALSE;
    return true;
}

// Recursively walk an IGameNode and its children, appending exportable
// meshes (and a per-mesh attachment bone) to `scene`.
void walk_node(IGameNode* node, alamo_format::ExportScene& scene)
{
    if (!node) return;

    IGameObject* obj = node->GetIGameObject();
    if (obj) {
        if (obj->GetIGameType() == IGameObject::IGAME_MESH) {
            // InitializeData is required before any mesh accessor calls.
            if (obj->InitializeData()) {
                IGameMesh* gmesh = static_cast<IGameMesh*>(obj);
                alamo_format::ExportMesh mesh;
                if (build_mesh(node, gmesh, mesh)) {
                    // Allocate the per-mesh attachment bone before pushing
                    // the mesh, so the mesh's bone_index is correct.
                    alamo_format::ExportBone bone;
                    bone.name           = mesh.name;
                    bone.parent_index   = 0;          // child of Root
                    bone.visible        = true;
                    bone.billboard_mode = 0;
                    // Identity matrix is the default; Phase 5 will bake
                    // the node's world transform here.
                    mesh.bone_index     = static_cast<std::uint32_t>(scene.bones.size());
                    scene.bones.push_back(std::move(bone));
                    scene.meshes.push_back(std::move(mesh));
                }
            }
        }
        node->ReleaseIGameObject();
    }

    for (int i = 0; i < node->GetChildCount(); ++i) {
        walk_node(node->GetNodeChild(i), scene);
    }
}

}  // namespace

// ---- Diagnostic log -------------------------------------------------------

const char* slot_name(int slot)
{
    switch (slot) {
        case ID_AM: return "Ambient";
        case ID_DI: return "Diffuse";
        case ID_SP: return "Specular";
        case ID_SH: return "Glossiness";
        case ID_SS: return "SpecLevel";
        case ID_SI: return "SelfIllum";
        case ID_OP: return "Opacity";
        case ID_FI: return "Filter";
        case ID_BU: return "Bump";
        case ID_RL: return "Reflection";
        case ID_RR: return "Refraction";
        case ID_DP: return "Displacement";
        default:    return "?";
    }
}

void log_node_material(IGameNode* node, std::ostringstream& os)
{
    if (!node) return;

    char node_name[256];
    std::snprintf(node_name, sizeof(node_name), "%s",
                  to_utf8(node->GetName()).c_str());
    os << "Node \"" << node_name << "\":\n";

    IGameMaterial* gmat = node->GetNodeMaterial();
    if (!gmat) {
        os << "  (no material)\n\n";
        return;
    }

    Mtl* maxmat = gmat->GetMaxMaterial();
    if (maxmat) {
        os << "  Max material class: \"" << to_utf8(maxmat->GetName()) << "\"";
        MSTR cn;
        maxmat->GetClassName(cn);
        if (cn.length() > 0) {
            os << " (type: " << to_utf8(cn.data()) << ")";
        }
        os << "\n";
    }

    if (IDxMaterial* dx = as_dx_material(maxmat)) {
        os << "  -> DXMaterial detected\n";
        const MSTR& fn = dx->GetEffectFile().GetFileName();
        os << "  effect file: \""
           << (fn.length() > 0 ? to_utf8(fn.data()) : std::string("(empty)"))
           << "\"\n";
        const int n = dx->GetNumberOfEffectBitmaps();
        os << "  effect bitmaps (" << n << "):\n";
        for (int i = 0; i < n; ++i) {
            PBBitmap* pb = dx->GetEffectBitmap(i);
            const TCHAR* path = pb ? pb->bi.Name() : nullptr;
            os << "    [" << i << "] \""
               << (path ? to_utf8(path) : std::string("(empty)"))
               << "\"\n";
        }
    } else {
        const int subs = gmat->GetSubMaterialCount();
        if (subs > 0) {
            os << "  Multi/Sub-Object with " << subs << " sub-materials (using sub[0])\n";
        }
        const int n = gmat->GetNumberOfTextureMaps();
        os << "  texture maps (" << n << "):\n";
        for (int i = 0; i < n; ++i) {
            IGameTextureMap* tex = gmat->GetIGameTextureMap(i);
            if (!tex) { os << "    [" << i << "] (null)\n"; continue; }
            const int slot = tex->GetStdMapSlot();
            const TCHAR* fn = tex->GetBitmapFileName();
            os << "    [" << i << "] slot=" << slot << " (" << slot_name(slot) << ")"
               << " file=\"" << (fn ? to_utf8(fn) : std::string("(empty)")) << "\"\n";
        }
    }

    ExtractedMaterial em = extract_material(gmat);

    // Surface any node-level shader-name override and apply it to the
    // diagnostic the same way build_mesh applies it to the export.
    INode* inode = node->GetMaxNode();
    std::string ovr = read_node_user_prop(inode, kShaderOverrideKey);
    if (!ovr.empty()) {
        os << "  Alamo_Shader_Name user property: \"" << ovr << "\" (overrides material)\n";
        em.shader_name = ovr;
        em.source_kind = "UserProp(Alamo_Shader_Name) + " + em.source_kind;
    } else {
        os << "  Alamo_Shader_Name user property: (not set)\n";
    }

    os << "  -> chosen for export:\n"
       << "       shader_name  = \"" << em.shader_name << "\"\n"
       << "       base_texture = \"" << em.base_texture << "\"\n"
       << "       source_kind  = " << em.source_kind << "\n\n";
}

void diag_walk_node(IGameNode* node, std::ostringstream& os)
{
    if (!node) return;
    if (IGameObject* obj = node->GetIGameObject()) {
        if (obj->GetIGameType() == IGameObject::IGAME_MESH) {
            log_node_material(node, os);
        }
        node->ReleaseIGameObject();
    }
    for (int i = 0; i < node->GetChildCount(); ++i) {
        diag_walk_node(node->GetNodeChild(i), os);
    }
}

// ---- Main entry points ----------------------------------------------------

bool walk_scene(Interface*                  /*max_interface*/,
                alamo_format::ExportScene&  out,
                std::string&                out_error)
{
    out = alamo_format::ExportScene::with_root_bone();
    out_error.clear();

    IGameScene* igame = GetIGameInterface();
    if (!igame) {
        out_error = "GetIGameInterface() returned null";
        return false;
    }

    // Keep Max's coordinate system; per Phase 0.5 RE the Alamo format
    // matches Max Z-up RH directly. No axis swap.
    if (IGameConversionManager* cm = GetConversionManager()) {
        cm->SetCoordSystem(IGameConversionManager::IGAME_MAX);
    }

    if (!igame->InitialiseIGame(/*selected=*/false)) {
        out_error = "IGameScene::InitialiseIGame() failed";
        return false;
    }
    igame->SetStaticFrame(0);

    const int top_count = igame->GetTopLevelNodeCount();
    for (int i = 0; i < top_count; ++i) {
        walk_node(igame->GetTopLevelNode(i), out);
    }

    igame->ReleaseIGame();
    return true;
}

void log_material_diagnostics(Interface* /*max_interface*/, std::string& out_log)
{
    std::ostringstream os;
    os << "max2alamo material diagnostics\n"
       << "==============================\n\n";

    IGameScene* igame = GetIGameInterface();
    if (!igame) {
        os << "(GetIGameInterface returned null)\n";
        out_log += os.str();
        return;
    }
    if (IGameConversionManager* cm = GetConversionManager()) {
        cm->SetCoordSystem(IGameConversionManager::IGAME_MAX);
    }
    if (!igame->InitialiseIGame(false)) {
        os << "(InitialiseIGame failed)\n";
        out_log += os.str();
        return;
    }
    igame->SetStaticFrame(0);

    const int top = igame->GetTopLevelNodeCount();
    os << "top-level node count: " << top << "\n\n";
    for (int i = 0; i < top; ++i) {
        diag_walk_node(igame->GetTopLevelNode(i), os);
    }

    igame->ReleaseIGame();
    out_log += os.str();
}

}  // namespace max2alamo

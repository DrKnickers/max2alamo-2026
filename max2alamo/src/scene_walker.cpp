#include "scene_walker.h"

#include <Max.h>
#include <maxapi.h>
#include <stdmat.h>   // ID_DI = standard-material diffuse slot constant

#include <IGame/IGame.h>
#include <IGame/IGameObject.h>
#include <IGame/IGameMaterial.h>
#include <IGame/IConversionManager.h>

#include <algorithm>
#include <limits>
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

// Look up the diffuse-slot bitmap filename on a Max material. Returns
// empty string if no diffuse texture is set or the material isn't a
// Standard material (in which case the engine will fall back to its
// default for the chosen shader). Multi/Sub-Object materials get
// flattened to their first sub-material for Phase 4 -- multi-material
// support comes later.
std::string extract_diffuse_texture(IGameMaterial* mat)
{
    if (!mat) return {};
    if (mat->GetSubMaterialCount() > 0) {
        if (IGameMaterial* sub = mat->GetSubMaterial(0)) {
            return extract_diffuse_texture(sub);
        }
    }
    const int n = mat->GetNumberOfTextureMaps();
    for (int i = 0; i < n; ++i) {
        IGameTextureMap* tex = mat->GetIGameTextureMap(i);
        if (!tex) continue;
        if (tex->GetStdMapSlot() != ID_DI) continue;
        const TCHAR* fn = tex->GetBitmapFileName();
        if (!fn) continue;
        return basename(to_utf8(fn));
    }
    return {};
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

    // Phase 4: single submesh, single material slot, fixed shader.
    // Multi-material support arrives later (face->matID buckets per
    // ExportSubmesh).
    alamo_format::ExportSubmesh sub;
    sub.material.shader_name  = "MeshAlpha.fx";
    sub.material.base_texture = extract_diffuse_texture(node->GetNodeMaterial());
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
// meshes to `scene`.
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

}  // namespace max2alamo

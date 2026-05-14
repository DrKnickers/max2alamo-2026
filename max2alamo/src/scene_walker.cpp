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
#include <IGame/IGameModifier.h>
#include <IGame/IConversionManager.h>

#include "alamo_format/shader_table.h"
#include "alamo_format/skin_weights.h"

#include "alamo_proxy_helper.h"  // for kAlamoProxyClassID

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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

// Phase 5d: typed user-prop readers for the Alamo_* family that the
// Utility command-panel UI writes. Each returns the prop's value when
// present, or the provided default when absent -- this is how the
// walker honours an explicit author choice while preserving the
// pre-5d "no prop = default behaviour" contract that the test corpus
// was built against.
bool read_node_user_prop_bool(INode* node, const TCHAR* key, bool dflt)
{
    if (!node || !key) return dflt;
    BOOL v = dflt ? TRUE : FALSE;
    if (node->GetUserPropBool(const_cast<TCHAR*>(key), v)) return v != FALSE;
    return dflt;
}

int read_node_user_prop_int(INode* node, const TCHAR* key, int dflt)
{
    if (!node || !key) return dflt;
    int v = dflt;
    if (node->GetUserPropInt(const_cast<TCHAR*>(key), v)) return v;
    return dflt;
}

// Returns true when the node has a user-prop named `key` at all, in
// any value -- used to distinguish "author set Alamo_Geometry_Hidden=0
// explicitly" from "prop absent, fall back to Max-native IsNodeHidden".
bool has_node_user_prop(INode* node, const TCHAR* key)
{
    if (!node || !key) return false;
    BOOL bv = FALSE;
    if (node->GetUserPropBool(const_cast<TCHAR*>(key), bv)) return true;
    int iv = 0;
    if (node->GetUserPropInt(const_cast<TCHAR*>(key), iv)) return true;
    MSTR sv;
    if (node->GetUserPropString(const_cast<TCHAR*>(key), sv)) return true;
    return false;
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
    std::vector<alamo_format::MaterialParam> params;  // typed parameter values
                                                      // pulled from the source material
                                                      // ParamBlock (Phase 6c).
};

// Read every parameter declared in `pblock` and convert it to a
// MaterialParam. Used on the DirectX Shader material's ParamBlock(0)
// which (per IDxMaterial docs) hosts the effect's parameters with names
// matching the .fx file. Unknown types are skipped silently -- the
// writer-side shader_table will fill them in with defaults.
std::vector<alamo_format::MaterialParam> extract_pblock_params(IParamBlock2* pblock)
{
    using alamo_format::MaterialParam;
    std::vector<MaterialParam> out;
    if (!pblock) return out;

    const int n = pblock->NumParams();
    out.reserve(static_cast<std::size_t>(n));

    const TimeValue t = 0;
    Interval iv = FOREVER;

    for (int i = 0; i < n; ++i) {
        const ParamID pid = pblock->IndextoID(i);
        const ParamDef& pdef = pblock->GetParamDef(pid);
        if (!pdef.int_name) continue;
        std::string name = to_utf8(pdef.int_name);
        if (name.empty()) continue;

        MaterialParam mp;
        mp.name = std::move(name);

        // Switch on the integer value: TYPE_FLOAT / TYPE_INT / TYPE_RGBA /
        // TYPE_POINT3 / TYPE_BOOL are macros (not enum members) in the Max
        // SDK so a `switch (pdef.type)` would warn under strict enum rules.
        switch (static_cast<int>(pdef.type)) {
            case TYPE_FLOAT: {
                float v = 0.f;
                pblock->GetValue(pid, t, v, iv);
                mp.kind = MaterialParam::Kind::Float;
                mp.value4 = { v, 0.f, 0.f, 0.f };
                out.push_back(std::move(mp));
                break;
            }
            case TYPE_INT: {
                int v = 0;
                pblock->GetValue(pid, t, v, iv);
                mp.kind = MaterialParam::Kind::Float;
                mp.value4 = { static_cast<float>(v), 0.f, 0.f, 0.f };
                out.push_back(std::move(mp));
                break;
            }
            case TYPE_RGBA:
            case TYPE_FRGBA: {
                AColor c;  // 4 floats; TYPE_RGBA leaves alpha at 1
                pblock->GetValue(pid, t, c, iv);
                mp.kind = MaterialParam::Kind::Float4;
                mp.value4 = { c.r, c.g, c.b, c.a };
                out.push_back(std::move(mp));
                break;
            }
            case TYPE_POINT3: {
                Point3 p;
                pblock->GetValue(pid, t, p, iv);
                mp.kind = MaterialParam::Kind::Float4;
                mp.value4 = { p.x, p.y, p.z, 0.f };  // 4th element pads to 0 per vanilla
                out.push_back(std::move(mp));
                break;
            }
            case TYPE_POINT4: {
                Point4 p;
                pblock->GetValue(pid, t, p, iv);
                mp.kind = MaterialParam::Kind::Float4;
                mp.value4 = { p.x, p.y, p.z, p.w };
                out.push_back(std::move(mp));
                break;
            }
            case TYPE_BITMAP: {
                PBBitmap* b = nullptr;
                pblock->GetValue(pid, t, b, iv);
                const TCHAR* path = (b ? b->bi.Name() : nullptr);
                if (path && path[0]) {
                    mp.kind = MaterialParam::Kind::Texture;
                    mp.texture = basename(to_utf8(path));
                    out.push_back(std::move(mp));
                }
                break;
            }
            default:
                // Unsupported types (TYPE_INODE / TYPE_STRING / matrix /
                // TYPE_BOOL etc.) are dropped -- the shader_table writer
                // falls back to defaults for these param names.
                break;
        }
    }
    return out;
}

// Node-level user property name. If set on a mesh node, its value
// overrides whatever shader name we'd otherwise pick from the material.
// This is the practical workaround for Max 2026's DirectX Shader
// material being unable to compile EaW's DX9-era HLSL: users put a
// plain Standard material on the mesh for the texture, then add this
// user property to choose the actual Alamo shader.
constexpr const TCHAR* kShaderOverrideKey   = _T("Alamo_Shader_Name");

// Phase 5d/5e: Alamo_* user-prop family written by the Utility panel.
// Names must match alamo_utility.cpp's kProp* constants byte-for-byte
// so the read side (here) sees what the write side (panel) stored.
constexpr const TCHAR* kPropExportTransform = _T("Alamo_Export_Transform");
constexpr const TCHAR* kPropExportGeometry  = _T("Alamo_Export_Geometry");
constexpr const TCHAR* kPropCollisionEnabled= _T("Alamo_Collision_Enabled");
constexpr const TCHAR* kPropGeometryHidden  = _T("Alamo_Geometry_Hidden");
constexpr const TCHAR* kPropBillboardMode   = _T("Alamo_Billboard_Mode");

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
            // Effect bitmap walk gives us the BaseTexture shortcut for
            // back-compat with legacy callers that read .base_texture.
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
            // ParamBlock(0) hosts the effect's typed parameter values
            // (Emissive, Diffuse, Specular, Colorization, ...). Pull
            // them all -- the writer side filters down to whatever the
            // shader_table declares for this shader and falls back to
            // defaults for missing entries.
            out.params = extract_pblock_params(maxmat->GetParamBlock(0));
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

// Walker-state passed into build_mesh so each vertex emission knows
// whether to use per-vertex skinning or rigid attachment to a per-mesh
// bone. `skin` is null for static / unskinned meshes; otherwise it's
// the IGameSkin modifier on the mesh, queried per Max-vertex-index.
// `bone_map` resolves IGameSkin's bone IGameNodes back to ExportScene
// bone indices (populated by walk_bones). `fallback_bone_index` is the
// rigid-attachment fallback used for static meshes AND for any skinned
// vertex whose dominant bone isn't in the map (defensive: keeps the
// geometry from collapsing to Root if a bone got dropped).
struct SkinContext {
    IGameSkin* skin = nullptr;
    const std::unordered_map<INode*, std::uint32_t>* bone_map = nullptr;
    std::uint32_t fallback_bone_index = 0;  // 0 = Root sentinel
};

// Pull every (ExportScene bone index, weight) pair for `vert_idx` from
// the IGameSkin modifier and pack the top 4 by weight (renormalized to
// sum to 1.0) into a VertexBinding.
//
// Bones whose IGameNode -> INode -> bone_map lookup fails are dropped
// defensively -- not folded into the fallback -- because a non-mapped
// bone usually means the Max scene has a skin influence pointing at a
// node we don't export (e.g. a stray Biped subtype the walker hasn't
// learned yet in Phase 5). Mapping it to the per-mesh fallback would
// quietly bind chunks of geometry to the wrong place; dropping it
// preserves the remaining mappable bones and surfaces the issue as a
// missing influence rather than a wrong one.
//
// If the input is fully empty / all-dropped / all-zero-weight, falls
// back to a rigid binding (slot 0 = ctx.fallback_bone_index, weight =
// 1). For static meshes the caller short-circuits before even getting
// here.
alamo_format::skin::VertexBinding
resolve_multi_bone(const SkinContext& ctx, int vert_idx)
{
    if (!ctx.skin || !ctx.bone_map) {
        return alamo_format::skin::top4_normalized({}, ctx.fallback_bone_index);
    }
    std::vector<alamo_format::skin::BoneWeight> influences;
    const int n_bones = ctx.skin->GetNumberOfBones(vert_idx);
    influences.reserve(static_cast<std::size_t>(n_bones));
    for (int b = 0; b < n_bones; ++b) {
        const float w = ctx.skin->GetWeight(vert_idx, b);
        if (w <= 0.f) continue;
        IGameNode* gbone = ctx.skin->GetIGameBone(vert_idx, b);
        if (!gbone) continue;
        INode* inode = gbone->GetMaxNode();
        if (!inode) continue;
        auto it = ctx.bone_map->find(inode);
        if (it == ctx.bone_map->end()) continue;
        influences.push_back({it->second, w});
    }
    return alamo_format::skin::top4_normalized(influences, ctx.fallback_bone_index);
}

// Build one ExportMesh from an IGameNode wrapping an IGameMesh. Returns
// std::nullopt-style empty optional via the bool return; mesh is filled
// only on success.
bool build_mesh(IGameNode* node, IGameMesh* gmesh,
                const SkinContext& skin_ctx,
                alamo_format::ExportMesh& out)
{
    out.name = to_utf8(node->GetName());

    const int face_count = gmesh->GetNumberOfFaces();
    if (face_count <= 0) {
        return false;  // skip degenerate / empty meshes
    }

    // Tangent / binormal availability. IGame populates these (MikkTSpace by
    // default in Max 2014+) when the mesh has a normal-mapped material or
    // the user enabled "Tangents and Bitangents" in preferences. If absent,
    // we still export -- the vertex chunk will carry zero tangents, the
    // .export.log notes it, and downstream bump shaders render incorrectly.
    // Phase 6b future: fall back to vendored MikkTSpace computation here so
    // tangent space is always written regardless of Max-side configuration.
    const int num_tangents = gmesh->GetNumberOfTangents();
    const bool have_tangents = num_tangents > 0;

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
    sub.material.params       = std::move(em.params);
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

            const int max_vert_idx = static_cast<int>(face->vert[corner]);
            const Point3 pos = gmesh->GetVertex(max_vert_idx, /*ObjectSpace=*/true);
            v.position = { pos.x, pos.y, pos.z };
            update_bbox(out, pos);
            bbox_seeded = true;

            const Point3 nrm = gmesh->GetNormal(static_cast<int>(face->norm[corner]),
                                                /*ObjectSpace=*/true);
            v.normal = { nrm.x, nrm.y, nrm.z };

            // Per-vertex skin binding (Phase 5c). For static meshes
            // skin_ctx.skin == null and resolve_multi_bone returns a
            // rigid binding to ctx.fallback_bone_index (the per-mesh
            // attachment bone allocated by walk_node). For skinned
            // meshes we read every (bone, weight) pair from the
            // IGameSkin modifier, keep the top 4 by weight, and
            // renormalize so slots 0..3 sum to 1.0.
            const auto binding = resolve_multi_bone(skin_ctx, max_vert_idx);
            v.bone_indices = binding.bone_indices;
            v.weights      = binding.weights;

            // Map channel 1 = standard UV channel in Max.
            const int tex_idx = static_cast<int>(face->texCoord[corner]);
            const Point2 tex = gmesh->GetTexVertex(tex_idx);
            // V-flip on write (Alamo / D3D convention vs Max convention).
            v.uv = { tex.x, 1.0f - tex.y };

            // Tangent-space basis. IGame keeps tangents in their own array,
            // indexed separately from texCoords -- you cannot reuse the
            // texCoord index here (Box meshes split tangents per face but
            // share texCoords across faces, so reusing texCoord index gives
            // tangent vectors from the wrong face). `GetFaceVertexTangentBinormal`
            // returns the correct shared index for both arrays, or -1 if
            // IGame didn't compute tangents for this mesh.
            if (have_tangents) {
                const int tb_idx = gmesh->GetFaceVertexTangentBinormal(f, corner);
                if (tb_idx >= 0 && tb_idx < num_tangents) {
                    const Point3 tan = gmesh->GetTangent(tb_idx);
                    const Point3 bin = gmesh->GetBinormal(tb_idx);
                    v.tangent  = { tan.x, tan.y, tan.z };
                    v.binormal = { bin.x, bin.y, bin.z };
                }
            }
            // else: tangent/binormal stay at the zero default. Logged once
            // per mesh via .export.log so the user can tell why bump-lit
            // shaders render flat.

            sub.vertices.push_back(v);
            sub.indices.push_back(static_cast<std::uint32_t>(sub.vertices.size() - 1u));
        }
    }

    if (!bbox_seeded) {
        out.bbox_min = { 0.f, 0.f, 0.f };
        out.bbox_max = { 0.f, 0.f, 0.f };
    }

    out.submeshes.push_back(std::move(sub));

    // Phase 5d: hidden / collision flags. Alamo_Geometry_Hidden, when
    // present on the node, takes precedence over Max's own hidden
    // state (modders sometimes need a mesh that's visible in-Max for
    // authoring convenience but `is_hidden=true` on disk, or vice
    // versa). Falls back to IsNodeHidden when the prop is absent,
    // preserving the pre-5d behaviour for scenes that never set the
    // prop. Alamo_Collision_Enabled defaults to false.
    INode* node_inode = node->GetMaxNode();
    if (has_node_user_prop(node_inode, kPropGeometryHidden)) {
        out.is_hidden = read_node_user_prop_bool(node_inode, kPropGeometryHidden, false);
    } else {
        out.is_hidden = node->IsNodeHidden() != FALSE;
    }
    out.is_collision = read_node_user_prop_bool(node_inode, kPropCollisionEnabled, false);
    return true;
}

// Phase 5g: compose a node's TM with its object offset, producing the
// matrix the engine sees at the bone slot. The IGame TM accessors
// (GetLocalTM / GetWorldTM / GetObjectTM) all return the node's TM
// WITHOUT the object offset. "Affect Pivot Only" in the Max Hierarchy
// panel (and MaxScript `node.objectoffsetrot = ...`) mutates the object
// offset; without composing it into the bone matrix, the rotation is
// silently lost on export. See issue #53 / probe_pivot_orientation*.ms.
//
// Composition order: row-vector convention. A vertex `v` at runtime is
// transformed as `v * bone_matrix`. We want the bone matrix to express
// the pivot's orientation in parent space, so that a point in pivot
// space transforms correctly:
//
//     point_parent = point_pivot * object_offset_TM * node_TM
//
// Hence: bone_matrix = object_offset_TM * node_TM (in that order).
//
// Identity object offset (the typical case for every existing harness
// test and every file in the vanilla corpus we have access to) makes
// this a no-op: result == node_tm byte-identical.
//
// Notes:
//   * We compose rotation + translation but NOT scale. Non-unit object
//     offset scale is exotic, not exercised by any test or corpus
//     file, and would require additional thought about how the engine
//     interprets a scaled bone matrix at runtime. Documented as out-
//     of-scope risk in the Phase 5g plan.
//   * BoneSys.createBone's "direction" arg encodes orientation in the
//     bone object's procedural-display data, NOT in either the node TM
//     or the object offset. That part of the legacy authoring workflow
//     is NOT recovered by this fix; users should explicitly rotate
//     bones via `node.rotation = ...` rather than relying on createBone
//     direction args.
Matrix3 compose_with_object_offset(INode* inode, const Matrix3& node_tm)
{
    if (!inode) return node_tm;

    Quat   off_rot = inode->GetObjOffsetRot();
    Point3 off_pos = inode->GetObjOffsetPos();

    Matrix3 offset_tm;
    off_rot.MakeMatrix(offset_tm);   // pure rotation Matrix3
    offset_tm.SetTrans(off_pos);

    return offset_tm * node_tm;
}

// Encode a Max Matrix3 into the alamo_format 4x3 column-major-by-element
// layout. Phase 4c established this for world TMs; Phase 5a reuses it
// for both world TMs (synthetic per-mesh bones) and local-to-parent TMs
// (real Max bones in the skeleton hierarchy).
std::array<float, 12> encode_matrix3(const Matrix3& m3)
{
    const Point3 row0 = m3.GetRow(0);  // X axis
    const Point3 row1 = m3.GetRow(1);  // Y axis
    const Point3 row2 = m3.GetRow(2);  // Z axis
    const Point3 trn  = m3.GetRow(3);  // translation
    return {
        row0.x, row1.x, row2.x, trn.x,
        row0.y, row1.y, row2.y, trn.y,
        row0.z, row1.z, row2.z, trn.z,
    };
}

// Forward decl -- defined in the Phase 7c.2 proxy-walker block below.
// Needed here so is_exportable_bone can mutex Alamo_Proxy helpers
// out of the bone-export path.
bool is_alamo_proxy_node(IGameNode* gnode);

// Is this IGameNode an "exportable bone"? Two categories accepted:
//   - IGAME_BONE      (Phase 5a): regular Max bones. Auto-exported;
//                     no user-prop opt-in required, to preserve the
//                     "every bone in the scene becomes a bone in the
//                     .alo" contract the test corpus relies on.
//   - IGAME_HELPER    (Phase 5e): Point / Dummy / Arrow helpers
//                     tagged with `Alamo_Export_Transform=true`,
//                     EXCLUDING Alamo_Proxy helpers (those are
//                     proxies, mutex via 7c.2).
// Future:
//   - IGAME_BIPED subtypes are wired up implicitly via IGAME_BONE
//     (Biped bones report as bones to IGame).
bool is_exportable_bone(IGameNode* node)
{
    if (!node) return false;
    IGameObject* obj = node->GetIGameObject();
    if (!obj) return false;
    const auto type = obj->GetIGameType();
    node->ReleaseIGameObject();

    if (type == IGameObject::IGAME_BONE) return true;
    if (type == IGameObject::IGAME_HELPER) {
        // Phase 7c.2 mutex: Alamo_Proxy helpers are proxies, never
        // bones. Even if the user (unusually) checked
        // Alamo_Export_Transform on a proxy node, the proxy walker
        // has already claimed it. Skip here so the helper-as-bone
        // path (Phase 5e) doesn't double-emit.
        if (is_alamo_proxy_node(node)) return false;
        return read_node_user_prop_bool(node->GetMaxNode(),
                                        kPropExportTransform, false);
    }
    return false;
}

// Phase 5a bone-hierarchy walk. Runs BEFORE the mesh walk in walk_scene
// so that scene.bones[0..N] are real Max bones (with local-to-parent
// matrices) before walk_node appends synthetic per-mesh attachment bones
// at indices N+1..
//
// `parent_bone_idx` is the index into scene.bones of the nearest
// ancestor that is itself an exportable bone. Top-level Max bones (or
// bones whose Max parents are not exportable) get parent_bone_idx = 0
// = our synthetic Root.
//
// Phase 5b: `bone_map` records (INode* -> ExportScene bone index) for
// every emitted bone, so walk_node can resolve IGameSkin's bone refs.
// Phase 8d: `visibility_map` is a BROADER (INode* -> bone_index) map than
// `bone_map`. Every emitted bone whose synth comes from a Max-side INode
// (real bones, helpers-as-bones, light synth bones, .Target siblings,
// proxy synth bones, static-mesh attachment bones) records itself here
// so walk_animation can sample INode::GetVisibility(t) per frame.
//
// We deliberately keep bone_map narrow (real bones + helpers-as-bones)
// so rotation/translation track emission isn't expanded to light/proxy/
// mesh synth bones -- that would break the Phase 8b/8c gold SHAs.
void walk_bones(IGameNode* node, std::uint32_t parent_bone_idx,
                alamo_format::ExportScene& scene,
                std::unordered_map<INode*, std::uint32_t>& bone_map,
                std::unordered_map<INode*, std::uint32_t>& visibility_map)
{
    if (!node) return;
    std::uint32_t my_idx = parent_bone_idx;
    if (is_exportable_bone(node)) {
        alamo_format::ExportBone bone;
        bone.name           = to_utf8(node->GetName());
        bone.parent_index   = parent_bone_idx;
        bone.visible        = node->IsNodeHidden() == FALSE;
        // Phase 5d: Alamo_Billboard_Mode on a real Max bone propagates
        // to the bone chunk's billboard_mode field. Engine reads this
        // to drive runtime billboard rotation (foliage, lens flares,
        // sun discs). Default 0 = Disable when prop absent.
        bone.billboard_mode = static_cast<std::uint32_t>(
            read_node_user_prop_int(node->GetMaxNode(), kPropBillboardMode, 0));
        // GetLocalTM returns the node's transform relative to its Max
        // parent. For top-level bones this is the world transform (Max
        // scene root is identity); for child bones it's the
        // parent-inverse-times-world product computed by IGame.
        // Phase 5g: compose with the bone's object offset (e.g. set via
        // Hierarchy -> Affect Pivot Only), which IGame omits from the
        // TM accessors but the engine expects in the on-disk bone slot.
        const Matrix3 node_tm = node->GetLocalTM().ExtractMatrix3();
        bone.matrix = encode_matrix3(
            compose_with_object_offset(node->GetMaxNode(), node_tm));
        my_idx = static_cast<std::uint32_t>(scene.bones.size());
        if (INode* max_inode = node->GetMaxNode()) {
            bone_map[max_inode]       = my_idx;
            visibility_map[max_inode] = my_idx;
        }
        scene.bones.push_back(std::move(bone));
    }
    for (int i = 0; i < node->GetChildCount(); ++i) {
        walk_bones(node->GetNodeChild(i), my_idx, scene, bone_map, visibility_map);
    }
}

// Phase 7b.1: light walker. Recursively walks the scene; for each
// IGAME_LIGHT node of supported type (Omni / Directional), pulls the
// light's properties via IGameLight + IGameProperty, encodes a
// synthetic per-light bone (matching the per-mesh-bone convention
// from Phase 4c), and pushes an ExportLight into the scene.
//
// Spotlights (IGAME_TSPOT / IGAME_FSPOT) need a sibling `.Target`
// bone to encode their orientation; that lands in 7b.2.

// IGameLight::LightType -> ExportLight::Type mapping. Returns
// std::nullopt for types we don't export.
std::optional<alamo_format::ExportLight::Type>
classify_light(IGameLight::LightType t)
{
    switch (t) {
    case IGameLight::IGAME_OMNI:
        return alamo_format::ExportLight::Type::Omni;
    case IGameLight::IGAME_DIR:
    case IGameLight::IGAME_TDIR:
        return alamo_format::ExportLight::Type::Directional;
    case IGameLight::IGAME_TSPOT:
    case IGameLight::IGAME_FSPOT:
        return alamo_format::ExportLight::Type::Spotlight;
    default:
        return std::nullopt;
    }
}

// Helpers to pull a static (frame-0) value out of an IGameProperty,
// returning the provided default if the property is missing or its
// declared data type doesn't match.
float read_light_float(IGameProperty* prop, float dflt = 0.f)
{
    if (!prop) return dflt;
    float v = dflt;
    if (!prop->GetPropertyValue(v)) return dflt;
    return v;
}

void read_light_color(IGameProperty* prop, std::array<float, 3>& out)
{
    if (!prop) return;
    Point3 p;
    if (prop->GetPropertyValue(p)) {
        // Max colors are stored 0..1 in IGameProperty (despite the UI
        // showing 0..255). No gamma conversion -- vanilla content uses
        // the raw values; the engine matches.
        out = {p.x, p.y, p.z};
    }
}

// Pulls every IGAME_LIGHT node in the scene (via IGame's typed
// lookup, which sidesteps the question of whether lights appear
// in the top-level traversal) and emits an ExportLight + synth
// bone for each Omni / Directional. Spotlights are skipped this
// phase (7b.2 will add them with their target-bone partner).
void walk_lights(IGameScene* igame, alamo_format::ExportScene& scene,
                 std::unordered_map<INode*, std::uint32_t>& visibility_map)
{
    if (!igame) return;
    Tab<IGameNode*> light_nodes =
        igame->GetIGameNodeByType(IGameObject::IGAME_LIGHT);

    for (int i = 0; i < light_nodes.Count(); ++i) {
        IGameNode* node = light_nodes[i];
        if (!node) continue;
        IGameObject* obj = node->GetIGameObject();
        if (!obj) continue;

        auto* gl = static_cast<IGameLight*>(obj);
        const auto kind = classify_light(gl->GetLightType());
        if (!kind.has_value()) {
            node->ReleaseIGameObject();
            continue;
        }

        alamo_format::ExportLight light;
        light.name = to_utf8(node->GetName());
        light.type = *kind;
        read_light_color(gl->GetLightColor(), light.color);
        light.intensity   = read_light_float(gl->GetLightMultiplier(), 1.f);
        light.atten_end   = read_light_float(gl->GetLightAttenEnd(),   0.f);
        light.atten_start = read_light_float(gl->GetLightAttenStart(), 0.f);
        if (light.type == alamo_format::ExportLight::Type::Spotlight) {
            // Phase 7b.2: spotlight cone. EMPIRICALLY: IGameProperty
            // returns light angles in DEGREES (not radians, despite
            // Max storing them as radians in the param block --
            // IGameProperty's float overload applies the same units
            // the MAXScript UI presents). Vanilla .alo content stores
            // RADIANS (e.g. EB_ICC_LANDINGPAD.ALO has falloff=0.7854
            // for what an artist would call a 45deg cone). So we
            // convert degrees -> radians at write time.
            //
            // Validated by exporting hotspot=30, falloff=45 from
            // MAXScript and confirming the .alo holds 0.5236 / 0.7854.
            const float hs_deg = read_light_float(gl->GetLightHotSpot(), 0.f);
            const float fo_deg = read_light_float(gl->GetLightFallOff(), 0.f);
            const float kDegToRad = 3.14159265358979323846f / 180.f;
            light.hotspot = hs_deg * kDegToRad;
            light.falloff = fo_deg * kDegToRad;
        } else {
            light.hotspot = 0.f;
            light.falloff = 0.f;
        }

        // Synthetic per-light bone (Phase 4c pattern). Name matches
        // the light so Mike's importer reconstructs them as paired
        // objects, and the .alo's connections can index into
        // scene.bones for the attachment.
        alamo_format::ExportBone synth_bone;
        synth_bone.name           = light.name;
        synth_bone.parent_index   = 0;  // child of Root
        synth_bone.visible        = node->IsNodeHidden() == FALSE;
        synth_bone.billboard_mode = 0;
        // Phase 5g: compose with the light's object offset so an
        // Affect-Pivot-Only rotation on the light propagates to the
        // synthetic bone (relevant for Spotlight cone-direction encoding
        // alongside the .Target sibling).
        {
            const Matrix3 light_tm = node->GetWorldTM().ExtractMatrix3();
            synth_bone.matrix = encode_matrix3(
                compose_with_object_offset(node->GetMaxNode(), light_tm));
        }

        light.bone_index = static_cast<std::uint32_t>(scene.bones.size());
        // Phase 8d: record the light's synth bone in visibility_map
        // BEFORE the push (we own the index now). Animated visibility on
        // the light propagates to the bone's 0x1007 track.
        if (INode* light_inode = node->GetMaxNode()) {
            visibility_map[light_inode] = light.bone_index;
        }
        scene.bones.push_back(std::move(synth_bone));

        // Phase 7b.2: if the light has a Max-side target node
        // (TargetSpot has one; FreeSpot does not), emit a sibling
        // bone at the target's world position so the engine /
        // Mike's importer can reconstruct orientation from the
        // <light, target> pair. Naming convention <name>.Target
        // matches vanilla EB_ICC_LANDINGPAD.ALO.
        if (INode* light_inode = node->GetMaxNode()) {
            if (INode* target_inode = light_inode->GetTarget()) {
                alamo_format::ExportBone tgt_bone;
                tgt_bone.name           = light.name + ".Target";
                tgt_bone.parent_index   = 0;
                tgt_bone.visible        = target_inode->IsNodeHidden() == FALSE;
                tgt_bone.billboard_mode = 0;
                // Target's world TM at the current time. snapshot at
                // frame 0 to match the rest of the export. Phase 5g:
                // compose with target node's object offset (typically
                // identity for Targetobject helpers but cheap to honour).
                Matrix3 tm = target_inode->GetNodeTM(0);
                tgt_bone.matrix = encode_matrix3(
                    compose_with_object_offset(target_inode, tm));
                // Phase 8d: record .Target sibling bone too.
                visibility_map[target_inode] =
                    static_cast<std::uint32_t>(scene.bones.size());
                scene.bones.push_back(std::move(tgt_bone));
            }
        }

        scene.lights.push_back(std::move(light));

        node->ReleaseIGameObject();
    }
}

// Recursively walk an IGameNode and its children, appending exportable
// meshes to `scene`. Two paths depending on whether the mesh has a Skin
// modifier:
//
//   - Static / unskinned (Phase 4c): allocate a synthetic per-mesh
//     attachment bone parented to Root with the mesh's WorldTM baked
//     in. The mesh connects to that bone; every vertex's slot-0 bone
//     index references it.
//
// Phase 7c.2: proxy walker. Iterates IGAME_HELPER nodes; any whose
// underlying object's Class_ID matches kAlamoProxyClassID becomes
// an ExportProxy. The Max-side node name is the proxy's reference
// name (e.g. "p_engine_glow"). Optional flags come from user props
// the Utility panel writes:
//   - Alamo_Geometry_Hidden          -> mini-chunk 7
//   - Alamo_Alt_Decrease_Stay_Hidden -> mini-chunk 8
// Both default false (then suppressed in the writer per vanilla
// omission semantics).
//
// Detection by Class_ID is explicit and unambiguous -- no name
// prefix matching. A plain Dummy helper named "p_smoke" does NOT
// export as a proxy; only Alamo_Proxy instances do. Matches the
// legacy Petroglyph plugin's design.
bool is_alamo_proxy_node(IGameNode* gnode)
{
    if (!gnode) return false;
    INode* inode = gnode->GetMaxNode();
    if (!inode) return false;
    Object* obj = inode->GetObjectRef();
    if (!obj) return false;
    return obj->ClassID() == kAlamoProxyClassID;
}

void walk_proxies(IGameScene* igame, alamo_format::ExportScene& scene,
                  std::unordered_map<INode*, std::uint32_t>& visibility_map)
{
    if (!igame) return;
    Tab<IGameNode*> helpers =
        igame->GetIGameNodeByType(IGameObject::IGAME_HELPER);

    for (int i = 0; i < helpers.Count(); ++i) {
        IGameNode* gnode = helpers[i];
        if (!is_alamo_proxy_node(gnode)) continue;

        INode* inode = gnode->GetMaxNode();
        alamo_format::ExportProxy proxy;
        proxy.name = to_utf8(gnode->GetName());

        // Optional flags from Alamo_* user props the Utility panel
        // writes. Hidden falls back to Max-native IsNodeHidden() when
        // the explicit prop is absent (mirrors Phase 5d mesh logic
        // and the per-node visibility convention).
        if (has_node_user_prop(inode, kPropGeometryHidden)) {
            proxy.is_hidden = read_node_user_prop_bool(inode, kPropGeometryHidden, false);
        } else {
            proxy.is_hidden = gnode->IsNodeHidden() != FALSE;
        }
        proxy.alt_decrease_stay_hidden = read_node_user_prop_bool(
            inode, _T("Alamo_Alt_Decrease_Stay_Hidden"), false);

        // Synthetic per-proxy bone (Phase 4c per-mesh-bone pattern;
        // matches Phase 7b's per-light bone). Parented to Root with
        // the proxy's world TM baked in. Bone name matches the
        // proxy so Mike's importer reconstructs Alamo_Proxy helpers
        // with the right names.
        alamo_format::ExportBone synth_bone;
        synth_bone.name           = proxy.name;
        synth_bone.parent_index   = 0;
        synth_bone.visible        = gnode->IsNodeHidden() == FALSE;
        synth_bone.billboard_mode = 0;
        // Phase 5g: compose with proxy node's object offset. This is
        // the main path for Alamo_Proxy hardpoints whose firing
        // direction was authored via Affect Pivot Only.
        {
            const Matrix3 proxy_tm = gnode->GetWorldTM().ExtractMatrix3();
            synth_bone.matrix = encode_matrix3(
                compose_with_object_offset(gnode->GetMaxNode(), proxy_tm));
        }

        proxy.bone_index = static_cast<std::uint32_t>(scene.bones.size());
        // Phase 8d: record proxy synth bone in visibility_map so animated
        // visibility on the helper propagates to the bone's 0x1007 track.
        visibility_map[inode] = proxy.bone_index;
        scene.bones.push_back(std::move(synth_bone));
        scene.proxies.push_back(std::move(proxy));
    }
}

//   - Skinned (Phase 5b/5c): no per-mesh bone. The mesh connects to
//     Root (matching vanilla -- see AI_DACTILLION.ALO's "object#0 ->
//     bone#0 (Root)" pattern); each vertex's top 4 influences (by
//     weight) populate slots 0..3 of bone_indices / weights via the
//     IGameSkin modifier, renormalized to sum to 1.0.
void walk_node(IGameNode* node, alamo_format::ExportScene& scene,
               const std::unordered_map<INode*, std::uint32_t>& bone_map,
               std::unordered_map<INode*, std::uint32_t>& visibility_map)
{
    if (!node) return;

    IGameObject* obj = node->GetIGameObject();
    if (obj) {
        if (obj->GetIGameType() == IGameObject::IGAME_MESH) {
            // Phase 5d: Alamo_Export_Geometry, when explicitly false,
            // skips the mesh entirely (matches the legacy PG plugin's
            // opt-out semantics for meshes the modder marked
            // non-exportable). Absent prop ⇒ export (preserves the
            // pre-5d "everything in the scene gets exported" contract
            // the test corpus relies on).
            INode* max_node = node->GetMaxNode();
            if (has_node_user_prop(max_node, kPropExportGeometry) &&
                !read_node_user_prop_bool(max_node, kPropExportGeometry, true)) {
                node->ReleaseIGameObject();
                for (int i = 0; i < node->GetChildCount(); ++i) {
                    walk_node(node->GetNodeChild(i), scene, bone_map, visibility_map);
                }
                return;
            }
            // InitializeData is required before any mesh accessor calls.
            if (obj->InitializeData()) {
                IGameMesh* gmesh = static_cast<IGameMesh*>(obj);
                IGameSkin* skin  = obj->GetIGameSkin();
                const bool is_skinned = (skin != nullptr);

                SkinContext ctx;
                ctx.skin     = skin;
                ctx.bone_map = &bone_map;

                // Pre-allocate the per-mesh attachment bone for static
                // meshes so build_mesh's per-vertex resolver can refer
                // to it via SkinContext::fallback_bone_index.
                alamo_format::ExportBone synth_bone;
                std::uint32_t connect_bone_index = 0;  // 0 = Root by default (skinned path)
                if (!is_skinned) {
                    synth_bone.name           = to_utf8(node->GetName());
                    synth_bone.parent_index   = 0;          // child of Root
                    // Phase 8f: honor Max-side IsNodeHidden, mirroring
                    // walk_lights/walk_proxies. Static-mesh synth bones
                    // were previously hardcoded visible=true regardless
                    // of the Max hidden flag -- a per-node-local-only
                    // visibility expression (per the corpus convention
                    // pinned by Phase 8f mesh_hierarchy test).
                    synth_bone.visible        = node->IsNodeHidden() == FALSE;
                    // Phase 5d: Alamo_Billboard_Mode on a static-mesh
                    // node propagates to the synthetic per-mesh bone
                    // that the engine animates as a billboard.
                    synth_bone.billboard_mode = static_cast<std::uint32_t>(
                        read_node_user_prop_int(max_node, kPropBillboardMode, 0));
                    // Phase 5g: compose with the static-mesh node's
                    // object offset (e.g. mesh-marker hardpoints when
                    // a future workflow allows Alamo_Export_Geometry=
                    // true while keeping pivot-rotation directions).
                    {
                        const Matrix3 mesh_tm = node->GetWorldTM().ExtractMatrix3();
                        synth_bone.matrix = encode_matrix3(
                            compose_with_object_offset(node->GetMaxNode(), mesh_tm));
                    }
                    connect_bone_index = static_cast<std::uint32_t>(scene.bones.size());
                    ctx.fallback_bone_index = connect_bone_index;
                }

                alamo_format::ExportMesh mesh;
                if (build_mesh(node, gmesh, ctx, mesh)) {
                    mesh.bone_index = connect_bone_index;
                    if (!is_skinned) {
                        // Phase 8d: record the static-mesh attachment
                        // bone in visibility_map so animated visibility
                        // on the mesh propagates to the bone's 0x1007
                        // track. Skinned meshes connect to Root and
                        // have no per-mesh bone -- their visibility
                        // comes from the bones they skin to (already
                        // tracked via walk_bones).
                        visibility_map[max_node] = connect_bone_index;
                        scene.bones.push_back(std::move(synth_bone));
                    }
                    scene.meshes.push_back(std::move(mesh));
                }
            }
        }
        node->ReleaseIGameObject();
    }

    for (int i = 0; i < node->GetChildCount(); ++i) {
        walk_node(node->GetNodeChild(i), scene, bone_map, visibility_map);
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

    // Tangent-space availability. IGame populates these (MikkTSpace by
    // default in Max 2014+) when the mesh has a normal-mapped material or
    // when "Tangents and Bitangents" is enabled in Preferences. If zero,
    // bump-mapped shaders render incorrectly because the .alo carries
    // zero tangent / binormal vectors.
    if (IGameObject* obj = node->GetIGameObject()) {
        if (obj->GetIGameType() == IGameObject::IGAME_MESH && obj->InitializeData()) {
            auto* gmesh = static_cast<IGameMesh*>(obj);
            const int nt = gmesh->GetNumberOfTangents();
            const int nb = gmesh->GetNumberOfBinormals();
            os << "  tangents: " << nt << "  binormals: " << nb;
            if (nt == 0 || nb == 0) {
                os << "  (WARNING: not computed -- bump shaders will render flat. "
                      "Enable normal map on material, or toggle Preferences -> "
                      "General -> 'Tangents and Bitangents' on)";
            }
            os << "\n";
            // Phase 5b: skin presence.
            if (IGameSkin* skin = obj->GetIGameSkin()) {
                os << "  skin: yes  (" << skin->GetNumOfSkinnedVerts()
                   << " skinned verts; connects to Root, per-vertex bone refs)\n";
            } else {
                os << "  skin: no   (rigid attachment to per-mesh synthetic bone)\n";
            }
        }
        node->ReleaseIGameObject();
    }

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
       << "       source_kind  = " << em.source_kind << "\n";

    // Per-parameter values pulled from the source ParamBlock (Phase 6c).
    // The writer further filters these to whatever the shader_table
    // declares for the shader, so a value listed here may or may not
    // appear in the on-disk material chunk. Useful for diagnosing
    // why a tweak didn't reach the .alo.
    if (!em.params.empty()) {
        os << "       params (" << em.params.size() << " from ParamBlock):\n";
        for (const auto& p : em.params) {
            os << "         " << p.name << " = ";
            switch (p.kind) {
                case alamo_format::MaterialParam::Kind::Float:
                    os << p.value4[0]; break;
                case alamo_format::MaterialParam::Kind::Float4:
                    os << "(" << p.value4[0] << ", " << p.value4[1] << ", "
                       << p.value4[2] << ", " << p.value4[3] << ")"; break;
                case alamo_format::MaterialParam::Kind::Texture:
                    os << "\"" << p.texture << "\""; break;
            }
            os << "\n";
        }
    }
    os << "\n";
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

// ---- Phase 8b: animation pass ---------------------------------------------

// Pack a Quat to int16 XYZW per the .ala convention (scale 32767, clamp
// to [-32767, 32767] so negation never overflows).
std::array<std::int16_t, 4> pack_quat_int16(const Quat& q)
{
    auto pack = [](float c) {
        float v = c * 32767.0f;
        if (v >  32767.0f) v =  32767.0f;
        if (v < -32767.0f) v = -32767.0f;
        return static_cast<std::int16_t>(std::lround(v));
    };
    return { pack(q.x), pack(q.y), pack(q.z), pack(q.w) };
}

// Phase 8d: pack per-frame visibility (bool per frame) to bytes per the
// .ala 0x1007 chunk convention. LSB-first per byte:
//   byte_idx 0 bit 0 = frame 0
//   byte_idx 0 bit 7 = frame 7
//   byte_idx 1 bit 0 = frame 8
//   ...
// Bit SET = visible at that frame; bit CLEAR = hidden. Output size is
// ceil(n_frames / 8) bytes; trailing bits in the last byte are 0.
//
// Reference: docs/format-notes.md:454-477 + alamo2max.ms:818-833.
std::vector<std::uint8_t> pack_visibility_bits(const std::vector<bool>& visible_per_frame)
{
    const std::size_t n = visible_per_frame.size();
    const std::size_t n_bytes = (n + 7) / 8;
    std::vector<std::uint8_t> out(n_bytes, std::uint8_t{0});
    for (std::size_t f = 0; f < n; ++f) {
        if (visible_per_frame[f]) {
            out[f / 8] |= static_cast<std::uint8_t>(1u << (f % 8));
        }
    }
    return out;
}

// Pack a per-frame translation Point3 to uint16[3] per the .ala convention
// (per-bone offset + scale unpacking). The result is bit-reinterpreted as
// int16[3] so the AlaAnimation::translation_pool (typed int16) can hold it
// alongside the rotation pool; the reader casts back to uint16 before
// applying offset + scale, so the bit pattern is what matters.
//
// Constant-axis guard: if scale[i] == 0 (axis didn't change across the
// range), packed[i] = 0; runtime unpacks to offset[i] + 0*0 = offset[i],
// which is the correct constant value.
std::array<std::int16_t, 3> pack_translation_uint16(
    const Point3& p, const float offset[3], const float scale[3])
{
    std::array<std::int16_t, 3> out{};
    const float pv[3] = { p.x, p.y, p.z };
    for (int i = 0; i < 3; ++i) {
        if (scale[i] > 0.0f) {
            float v = std::round((pv[i] - offset[i]) / scale[i]);
            if (v < 0.0f)     v = 0.0f;
            if (v > 65535.0f) v = 65535.0f;
            const std::uint16_t u = static_cast<std::uint16_t>(v);
            std::int16_t s;
            std::memcpy(&s, &u, sizeof(s));
            out[i] = s;
        } else {
            out[i] = 0;
        }
    }
    return out;
}

// Extract rotation-only Quat from a Matrix3 (drop scale by normalising
// each column of the upper-left 3x3 to unit length, then construct a
// Quat from the resulting rotation matrix).
Quat extract_rotation_quat(const Matrix3& m3_in)
{
    Matrix3 m3 = m3_in;
    Point3 row0 = m3.GetRow(0);
    Point3 row1 = m3.GetRow(1);
    Point3 row2 = m3.GetRow(2);
    auto safe_norm = [](Point3 p) {
        float n = p.Length();
        if (n < 1e-9f) return Point3(1.f, 0.f, 0.f);
        return p / n;
    };
    row0 = safe_norm(row0);
    row1 = safe_norm(row1);
    row2 = safe_norm(row2);
    m3.SetRow(0, row0);
    m3.SetRow(1, row1);
    m3.SetRow(2, row2);
    m3.SetRow(3, Point3(0.f, 0.f, 0.f));
    return Quat(m3);
}

// User-prop keys for clip authoring (read on the scene-root node).
const TCHAR* kPropAnimStart = _T("Alamo_Anim_Start");
const TCHAR* kPropAnimEnd   = _T("Alamo_Anim_End");
const TCHAR* kPropAnimName  = _T("Alamo_Anim_Name");
// Phase 11b: pipe-delimited clip-name list. Each `<NAME>` pairs with
// `Alamo_Anim_<NAME>_Start` / `_End` for that clip's frame range.
const TCHAR* kPropAnimClips = _T("Alamo_Anim_Clips");

// Split a pipe-delimited clip-name list. Empty fields and whitespace-
// only fields are dropped (the user wrote "FOO||BAR" or "FOO| |BAR").
// Clip-name validation (uppercase/digits/underscore) is enforced at
// authoring time in the Utility panel; the walker is permissive here
// to avoid blocking a partial export on a single bad name.
std::vector<std::string> split_clip_names(const std::string& list)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : list) {
        if (c == '|') {
            // Trim ASCII whitespace.
            std::size_t b = 0, e = cur.size();
            while (b < e && (cur[b] == ' ' || cur[b] == '\t')) ++b;
            while (e > b && (cur[e-1] == ' ' || cur[e-1] == '\t')) --e;
            if (e > b) out.emplace_back(cur.substr(b, e - b));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    std::size_t b = 0, e = cur.size();
    while (b < e && (cur[b] == ' ' || cur[b] == '\t')) ++b;
    while (e > b && (cur[e-1] == ' ' || cur[e-1] == '\t')) --e;
    if (e > b) out.emplace_back(cur.substr(b, e - b));
    return out;
}

// Inner per-clip sampler. Fills `out_anim` for one (start, end) range
// using the same bone-track / pool / visibility logic as the original
// Phase 8 single-clip path -- only the time range and pool sizes vary
// across clips. `start <= end` is the caller's responsibility (already
// validated by walk_animations); we still defensively short-circuit on
// invalid ranges so unit-test fuzzers can't drive a pool-size overflow.
void sample_clip_animation(IGameScene* igame,
                           Interface* max_interface,
                           const std::unordered_map<INode*, std::uint32_t>& bone_map,
                           const std::unordered_map<INode*, std::uint32_t>& visibility_map,
                           const alamo_format::ExportScene& scene,
                           int start,
                           int end,
                           alamo_format::AlaAnimation& out_anim)
{
    out_anim = alamo_format::AlaAnimation{};

    if (!max_interface || !igame) return;
    if (start < 0 || end < 0 || end < start) return;

    const int n_frames = end - start + 1;

    // Build the reverse map: bone-index -> INode*, with nullptr for
    // synthetic bones that are not in bone_map.
    std::vector<INode*> animatable(scene.bones.size(), nullptr);
    for (const auto& kv : bone_map) {
        if (kv.second < animatable.size()) animatable[kv.second] = kv.first;
    }
    // Map each animatable INode* back to its IGameNode* (so we can use
    // GetLocalTM(t), which is on IGameNode rather than INode).
    std::unordered_map<INode*, IGameNode*> max_to_igame;
    const int top = igame->GetTopLevelNodeCount();
    // Recursive lambda via std::function (cheap; called O(scene-node-count)).
    std::function<void(IGameNode*)> visit = [&](IGameNode* g) {
        if (!g) return;
        if (INode* mn = g->GetMaxNode()) max_to_igame[mn] = g;
        for (int j = 0; j < g->GetChildCount(); ++j) visit(g->GetNodeChild(j));
    };
    for (int i = 0; i < top; ++i) visit(igame->GetTopLevelNode(i));

    // First pass: create one AlaBoneTrack per ExportScene bone; assign
    // idx_rotation AND idx_translation slots for each animatable bone
    // (Phase 8b assigned rotation only; Phase 8c also assigns translation).
    out_anim.is_foc   = true;
    out_anim.n_frames = static_cast<std::uint32_t>(n_frames);
    out_anim.fps      = static_cast<float>(::GetFrameRate());
    out_anim.bones.reserve(scene.bones.size());

    std::int16_t rot_next   = 0;
    std::int16_t trans_next = 0;
    for (std::size_t i = 0; i < scene.bones.size(); ++i) {
        alamo_format::AlaBoneTrack t;
        t.name            = scene.bones[i].name;
        t.skeleton_index  = static_cast<std::uint32_t>(i);
        if (animatable[i] != nullptr) {
            t.idx_rotation    = rot_next;
            t.idx_translation = trans_next;
            rot_next   = static_cast<std::int16_t>(rot_next + 4);
            trans_next = static_cast<std::int16_t>(trans_next + 3);
        }
        // idx_scale stays at -1 (struct default). Scale tracks are absent
        // from vanilla FoC content (0/1500 corpus files have nScaleWords>0)
        // and the format defines no scale-pool chunk ID.
        out_anim.bones.push_back(std::move(t));
    }
    out_anim.n_rotation_words    = static_cast<std::uint32_t>(rot_next);
    out_anim.n_translation_words = static_cast<std::uint32_t>(trans_next);
    out_anim.n_scale_words       = 0;

    // Set default_rotation per bone (sample at start frame).
    const int ticks_per_frame = ::GetTicksPerFrame();
    const TimeValue t_start   = TimeValue(start * ticks_per_frame);
    for (std::size_t i = 0; i < out_anim.bones.size(); ++i) {
        if (!animatable[i]) continue;
        auto it = max_to_igame.find(animatable[i]);
        if (it == max_to_igame.end()) continue;
        // Phase 5g: compose with the bone's object offset so animation
        // tracks reflect the same pivot orientation as the static 0x206
        // bone matrix. (Object offset is typically static across frames;
        // sampling here at t_start captures it cleanly.)
        const Matrix3 node_tm = it->second->GetLocalTM(t_start).ExtractMatrix3();
        const Matrix3 m = compose_with_object_offset(animatable[i], node_tm);
        const Quat q   = extract_rotation_quat(m);
        out_anim.bones[i].default_rotation = pack_quat_int16(q);
    }

    // Phase 8d: don't early-return when rot_next == 0; the visibility pass
    // below uses `visibility_map` (broader than bone_map) and may emit
    // 0x1007 even on scenes with zero rotation/translation tracks (e.g.
    // a lone blinking light).
    if (rot_next > 0) {
        // Allocate the rotation pool: nFrames * nRotationWords int16.
        out_anim.rotation_pool.assign(
            static_cast<std::size_t>(n_frames) * static_cast<std::size_t>(rot_next),
            std::int16_t{0});
    }

    // Phase 8c: also collect raw per-frame translations so the second
    // sweep can compute per-bone min/max -> offset/scale -> uint16 packing.
    // One Point3 per (animatable bone, frame). Synthetic bones get empty
    // vectors (we never sample them).
    std::vector<std::vector<Point3>> raw_trans(scene.bones.size());
    for (std::size_t i = 0; i < scene.bones.size(); ++i) {
        if (animatable[i] != nullptr) {
            raw_trans[i].assign(static_cast<std::size_t>(n_frames), Point3(0.f, 0.f, 0.f));
        }
    }

    // Second pass: sample per frame. For each animatable bone we call
    // GetLocalTM(t) once and extract BOTH rotation and translation from
    // the resulting Matrix3 (no extra Max overhead vs Phase 8b).
    std::vector<Quat> previous_quat(scene.bones.size(), Quat(0.f, 0.f, 0.f, 1.f));
    std::vector<std::uint8_t> seen(scene.bones.size(), 0);  // 0 = first-frame placeholder
    for (int f = 0; f < n_frames; ++f) {
        const TimeValue t = TimeValue((start + f) * ticks_per_frame);
        for (std::size_t i = 0; i < out_anim.bones.size(); ++i) {
            if (out_anim.bones[i].idx_rotation < 0) continue;
            auto it = max_to_igame.find(animatable[i]);
            if (it == max_to_igame.end()) continue;
            // Phase 5g: compose with the bone's object offset per frame
            // so animation tracks see the pivot rotation consistently.
            const Matrix3 node_tm = it->second->GetLocalTM(t).ExtractMatrix3();
            const Matrix3 m = compose_with_object_offset(animatable[i], node_tm);

            // Rotation (8b path, unchanged): extract Quat + sign-canonicalise + pack.
            Quat q = extract_rotation_quat(m);
            if (seen[i]) {
                const Quat& p = previous_quat[i];
                const float d = q.x*p.x + q.y*p.y + q.z*p.z + q.w*p.w;
                if (d < 0.f) {
                    q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w;
                }
            }
            previous_quat[i] = q;
            seen[i] = 1;
            const auto packed_q = pack_quat_int16(q);
            const std::size_t base_rot =
                static_cast<std::size_t>(f) * static_cast<std::size_t>(rot_next)
                + static_cast<std::size_t>(out_anim.bones[i].idx_rotation);
            out_anim.rotation_pool[base_rot + 0] = packed_q[0];
            out_anim.rotation_pool[base_rot + 1] = packed_q[1];
            out_anim.rotation_pool[base_rot + 2] = packed_q[2];
            out_anim.rotation_pool[base_rot + 3] = packed_q[3];

            // Translation (8c new): stash raw Point3 for the post-loop
            // min/max scan and packing pass.
            raw_trans[i][static_cast<std::size_t>(f)] = m.GetRow(3);
        }
    }

    // Phase 8c: post-loop translation packing. For each animatable bone:
    //   - scan raw_trans[i] to compute per-axis min/max
    //   - set bones[i].trans_offset = min; trans_scale = (max-min)/65535
    //   - pack each frame's Point3 to uint16[3]; write to translation_pool
    if (trans_next > 0) {
        out_anim.translation_pool.assign(
            static_cast<std::size_t>(n_frames) * static_cast<std::size_t>(trans_next),
            std::int16_t{0});
        for (std::size_t i = 0; i < scene.bones.size(); ++i) {
            if (out_anim.bones[i].idx_translation < 0) continue;
            const auto& samples = raw_trans[i];
            // Init min/max with frame 0.
            Point3 minp = samples[0];
            Point3 maxp = samples[0];
            for (std::size_t f = 1; f < samples.size(); ++f) {
                const Point3& p = samples[f];
                if (p.x < minp.x) minp.x = p.x;
                if (p.y < minp.y) minp.y = p.y;
                if (p.z < minp.z) minp.z = p.z;
                if (p.x > maxp.x) maxp.x = p.x;
                if (p.y > maxp.y) maxp.y = p.y;
                if (p.z > maxp.z) maxp.z = p.z;
            }
            float offset[3] = { minp.x, minp.y, minp.z };
            float scale[3]  = {
                (maxp.x - minp.x) / 65535.0f,
                (maxp.y - minp.y) / 65535.0f,
                (maxp.z - minp.z) / 65535.0f,
            };
            // Snap negative-zero / sub-epsilon scale to exactly 0 so the
            // packer's `scale[i] > 0` guard fires deterministically.
            for (int axis = 0; axis < 3; ++axis) {
                if (scale[axis] < 1e-9f) scale[axis] = 0.0f;
            }
            for (int axis = 0; axis < 3; ++axis) {
                out_anim.bones[i].trans_offset[axis] = offset[axis];
                out_anim.bones[i].trans_scale[axis]  = scale[axis];
            }
            for (int f = 0; f < n_frames; ++f) {
                const auto packed_t = pack_translation_uint16(samples[f], offset, scale);
                const std::size_t base_trans =
                    static_cast<std::size_t>(f) * static_cast<std::size_t>(trans_next)
                    + static_cast<std::size_t>(out_anim.bones[i].idx_translation);
                out_anim.translation_pool[base_trans + 0] = packed_t[0];
                out_anim.translation_pool[base_trans + 1] = packed_t[1];
                out_anim.translation_pool[base_trans + 2] = packed_t[2];
            }
        }
    }

    // Phase 8d: visibility tracks. THIRD pass over visibility_map (which
    // is broader than bone_map -- includes light, proxy, and static-mesh
    // attachment bones). For each source INode, sample
    // INode::GetVisibility(t) per frame; threshold at 0.5. If ANY frame
    // is hidden, append a 0x1007 leaf chunk to that bone's
    // track_leaves (which AlaBoneTrack carries verbatim through
    // build_ala from Phase 8a). Constant-visible bones emit no chunk --
    // matches vanilla "constant-visible elision".
    //
    // Iteration order over an unordered_map is unspecified, but the
    // per-bone emit goes into `out_anim.bones[bone_idx].track_leaves`,
    // which is indexed by scene.bones order. The on-disk write order
    // (build_ala) walks `out_anim.bones` linearly, so output bytes are
    // deterministic regardless of map iteration order.
    constexpr float kVisibleThreshold = 0.5f;
    for (const auto& [inode, bone_idx] : visibility_map) {
        if (!inode) continue;
        if (bone_idx >= out_anim.bones.size()) continue;
        std::vector<bool> visible(static_cast<std::size_t>(n_frames), true);
        bool any_hidden = false;
        for (int f = 0; f < n_frames; ++f) {
            const TimeValue t = TimeValue((start + f) * ticks_per_frame);
            const float v = inode->GetVisibility(t, nullptr);
            const bool is_visible = (v >= kVisibleThreshold);
            visible[static_cast<std::size_t>(f)] = is_visible;
            if (!is_visible) any_hidden = true;
        }
        if (any_hidden) {
            auto bytes = pack_visibility_bits(visible);
            alamo_format::ChunkNode vis;
            vis.id = 0x1007;
            vis.is_container = false;
            vis.payload = std::move(bytes);
            out_anim.bones[bone_idx].track_leaves.push_back(std::move(vis));
        }
    }
}

// Phase 11b: read clip metadata from the scene-root node and dispatch
// per-clip sampling. Multi-clip path (Alamo_Anim_Clips set) wins over
// the un-suffixed single-clip path when both are authored; only the
// declared multi-clip list is sampled. Empty `out_clips` on return =
// no .ala emission (Phase 10b regression guard preserved).
void walk_animations(IGameScene* igame,
                     Interface* max_interface,
                     const std::unordered_map<INode*, std::uint32_t>& bone_map,
                     const std::unordered_map<INode*, std::uint32_t>& visibility_map,
                     const alamo_format::ExportScene& scene,
                     std::vector<ClipAnimation>& out_clips)
{
    out_clips.clear();
    if (!max_interface || !igame) return;
    INode* root = max_interface->GetRootNode();
    if (!root) return;

    const std::string clips_list = read_node_user_prop(root, kPropAnimClips);
    if (!clips_list.empty()) {
        // Multi-clip path. For each declared clip, read per-clip
        // _Start / _End user props and sample the range. Per-clip
        // failures (missing _Start or _End, or invalid range) skip
        // that clip; other clips still emit (partial-export policy
        // confirmed in 11a wrap, user 2026-05-13).
        const auto names = split_clip_names(clips_list);
        out_clips.reserve(names.size());
        for (const auto& name : names) {
            // Build the per-clip prop keys. TCHAR is wchar_t under
            // /D UNICODE; assemble in wide.
            std::wstring wname(name.begin(), name.end());
            const std::wstring k_start = L"Alamo_Anim_" + wname + L"_Start";
            const std::wstring k_end   = L"Alamo_Anim_" + wname + L"_End";
            const int start = read_node_user_prop_int(root, k_start.c_str(), -1);
            const int end   = read_node_user_prop_int(root, k_end.c_str(),   -1);
            if (start < 0 || end < 0 || end < start) continue;  // skip invalid clip
            ClipAnimation clip;
            clip.name = name;
            sample_clip_animation(igame, max_interface, bone_map, visibility_map,
                                  scene, start, end, clip.anim);
            out_clips.push_back(std::move(clip));
        }
        return;
    }

    // Single-clip un-suffixed back-compat path (Phase 8b/c/d shape).
    // Phase 10b sentinel (-1) distinguishes "prop absent" from "prop
    // explicitly = 0" so a static scene without authoring stays silent.
    const int start = read_node_user_prop_int(root, kPropAnimStart, -1);
    const int end   = read_node_user_prop_int(root, kPropAnimEnd,   -1);
    if (start < 0 || end < 0 || end < start) return;

    ClipAnimation clip;
    clip.name.clear();  // empty -> exporter writes bare <basename>.ala
    sample_clip_animation(igame, max_interface, bone_map, visibility_map,
                          scene, start, end, clip.anim);
    out_clips.push_back(std::move(clip));
}

// ---- Main entry points ----------------------------------------------------

bool walk_scene(Interface*                  max_interface,
                alamo_format::ExportScene&  out_scene,
                std::vector<ClipAnimation>& out_clips,
                std::string&                out_error)
{
    out_scene = alamo_format::ExportScene::with_root_bone();
    out_clips.clear();
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

    // Pass 1 (Phase 5a): real Max bones with local-to-parent matrices.
    // Must run before the mesh walk so scene.bones[0..N] are the real
    // skeleton before walk_node appends synthetic per-mesh bones.
    // bone_map records (Max INode* -> ExportScene bone index) for the
    // mesh-walk's skin resolver.
    //
    // Phase 8d: visibility_map is broader -- includes light/proxy/static-
    // mesh synth bones too. Kept distinct from bone_map so rotation/
    // translation track emission (which iterates bone_map) doesn't expand
    // to those bones and break 8b/8c gold SHAs.
    std::unordered_map<INode*, std::uint32_t> bone_map;
    std::unordered_map<INode*, std::uint32_t> visibility_map;
    for (int i = 0; i < top_count; ++i) {
        walk_bones(igame->GetTopLevelNode(i), /*parent_bone_idx=*/0,
                   out_scene, bone_map, visibility_map);
    }

    // Pass 2: meshes. Skinned meshes attach to Root and reference real
    // bones per-vertex; static meshes still get a synthetic per-mesh
    // attachment bone (Phase 4c).
    for (int i = 0; i < top_count; ++i) {
        walk_node(igame->GetTopLevelNode(i), out_scene, bone_map, visibility_map);
    }

    // Pass 3 (Phase 7b.1/2): lights, including spotlights with their
    // .Target sibling bones.
    walk_lights(igame, out_scene, visibility_map);

    // Pass 4 (Phase 7c.2): Alamo_Proxy helpers (particle/effect
    // attachment points). Class_ID-based detection -- not name
    // prefix; users explicitly place these via Create > Helpers >
    // Standard > Alamo Proxy.
    walk_proxies(igame, out_scene, visibility_map);

    // Pass 5 (Phase 8b/8c/8d, extended in 11b): animation. Samples
    // real Max bones + helpers-as-bones in bone_map for rotation/
    // translation tracks, and every bone in visibility_map for the
    // per-frame visibility track (0x1007). Reads clip metadata
    // (Alamo_Anim_Clips for multi-clip or Alamo_Anim_Start/_End/_Name
    // for single-clip back-compat) from the scene root. If no clip
    // authored, out_clips stays empty (caller skips .ala write).
    walk_animations(igame, max_interface, bone_map, visibility_map, out_scene, out_clips);

    igame->ReleaseIGame();
    return true;
}

void log_material_diagnostics(Interface* /*max_interface*/, std::string& out_log)
{
    std::ostringstream os;
    os << "max2alamo material diagnostics\n"
       << "==============================\n\n"
       // Coordinate-frame banner: max2alamo writes vertex positions,
       // normals, tangents and bone matrices in Max's native frame with
       // no axis remapping. EaW expects -Y as the model's forward axis
       // (empirically confirmed: vanilla ships like the Executor SD have
       // their engines at +Y and bow at -Y). Author with the model's
       // nose pointing toward -Y and it will fly correctly in-game.
       << "Coordinate frame: Z up, -Y forward, +X right (Max-native; matches EaW engine)\n\n";

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

void log_scene_summary(const alamo_format::ExportScene& scene, std::string& out_log)
{
    std::ostringstream os;
    os << "\n\nExportScene summary\n"
       << "===================\n"
       << "bones:       " << scene.bones.size()   << "\n"
       << "meshes:      " << scene.meshes.size()  << "\n"
       << "lights:      " << scene.lights.size()  << "\n"
       << "proxies:     " << scene.proxies.size() << "\n";

    if (!scene.proxies.empty()) {
        os << "\nProxies:\n";
        for (std::size_t i = 0; i < scene.proxies.size(); ++i) {
            const auto& p = scene.proxies[i];
            os << "  [" << i << "] \"" << p.name << "\""
               << "  bone=" << p.bone_index
               << "  hidden=" << (p.is_hidden ? "true" : "false")
               << "  altDecStayHidden=" << (p.alt_decrease_stay_hidden ? "true" : "false")
               << "\n";
        }
    }

    if (!scene.lights.empty()) {
        os << "\nLights:\n";
        for (std::size_t i = 0; i < scene.lights.size(); ++i) {
            const auto& l = scene.lights[i];
            const char* type_name = "?";
            switch (l.type) {
            case alamo_format::ExportLight::Type::Omni:        type_name = "Omni"; break;
            case alamo_format::ExportLight::Type::Directional: type_name = "Directional"; break;
            case alamo_format::ExportLight::Type::Spotlight:   type_name = "Spotlight"; break;
            }
            os << "  [" << i << "] \"" << l.name << "\""
               << "  type=" << type_name
               << "  color=(" << l.color[0] << ", " << l.color[1] << ", " << l.color[2] << ")"
               << "  intensity=" << l.intensity
               << "  atten=[" << l.atten_start << ".." << l.atten_end << "]"
               << "  bone=" << l.bone_index;
            if (l.type == alamo_format::ExportLight::Type::Spotlight) {
                os << "  cone=[" << l.hotspot << ".." << l.falloff << "] rad";
            }
            os << "\n";
        }
    }
    out_log += os.str();
}

}  // namespace max2alamo

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

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

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

// Is this IGameNode an "exportable bone"? Two categories accepted:
//   - IGAME_BONE      (Phase 5a): regular Max bones. Auto-exported;
//                     no user-prop opt-in required, to preserve the
//                     "every bone in the scene becomes a bone in the
//                     .alo" contract the test corpus relies on.
//   - IGAME_HELPER    (Phase 5e): Point / Dummy / Arrow helpers
//                     tagged with `Alamo_Export_Transform=true`.
//                     Opt-in because most scenes have helpers that
//                     aren't meant to be exported (lookat targets,
//                     authoring rigs, reference geometry, etc.).
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
void walk_bones(IGameNode* node, std::uint32_t parent_bone_idx,
                alamo_format::ExportScene& scene,
                std::unordered_map<INode*, std::uint32_t>& bone_map)
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
        // parent-inverse-times-world product computed by IGame. Either
        // way, this is exactly what vanilla 0x206 chunks store.
        const GMatrix local_tm = node->GetLocalTM();
        bone.matrix = encode_matrix3(local_tm.ExtractMatrix3());
        my_idx = static_cast<std::uint32_t>(scene.bones.size());
        if (INode* max_inode = node->GetMaxNode()) {
            bone_map[max_inode] = my_idx;
        }
        scene.bones.push_back(std::move(bone));
    }
    for (int i = 0; i < node->GetChildCount(); ++i) {
        walk_bones(node->GetNodeChild(i), my_idx, scene, bone_map);
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
//   - Skinned (Phase 5b/5c): no per-mesh bone. The mesh connects to
//     Root (matching vanilla -- see AI_DACTILLION.ALO's "object#0 ->
//     bone#0 (Root)" pattern); each vertex's top 4 influences (by
//     weight) populate slots 0..3 of bone_indices / weights via the
//     IGameSkin modifier, renormalized to sum to 1.0.
void walk_node(IGameNode* node, alamo_format::ExportScene& scene,
               const std::unordered_map<INode*, std::uint32_t>& bone_map)
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
                    walk_node(node->GetNodeChild(i), scene, bone_map);
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
                    synth_bone.visible        = true;
                    // Phase 5d: Alamo_Billboard_Mode on a static-mesh
                    // node propagates to the synthetic per-mesh bone
                    // that the engine animates as a billboard.
                    synth_bone.billboard_mode = static_cast<std::uint32_t>(
                        read_node_user_prop_int(max_node, kPropBillboardMode, 0));
                    synth_bone.matrix = encode_matrix3(node->GetWorldTM().ExtractMatrix3());
                    connect_bone_index = static_cast<std::uint32_t>(scene.bones.size());
                    ctx.fallback_bone_index = connect_bone_index;
                }

                alamo_format::ExportMesh mesh;
                if (build_mesh(node, gmesh, ctx, mesh)) {
                    mesh.bone_index = connect_bone_index;
                    if (!is_skinned) {
                        scene.bones.push_back(std::move(synth_bone));
                    }
                    scene.meshes.push_back(std::move(mesh));
                }
            }
        }
        node->ReleaseIGameObject();
    }

    for (int i = 0; i < node->GetChildCount(); ++i) {
        walk_node(node->GetNodeChild(i), scene, bone_map);
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

    // Pass 1 (Phase 5a): real Max bones with local-to-parent matrices.
    // Must run before the mesh walk so scene.bones[0..N] are the real
    // skeleton before walk_node appends synthetic per-mesh bones.
    // bone_map records (Max INode* -> ExportScene bone index) for the
    // mesh-walk's skin resolver.
    std::unordered_map<INode*, std::uint32_t> bone_map;
    for (int i = 0; i < top_count; ++i) {
        walk_bones(igame->GetTopLevelNode(i), /*parent_bone_idx=*/0, out, bone_map);
    }

    // Pass 2: meshes. Skinned meshes attach to Root and reference real
    // bones per-vertex; static meshes still get a synthetic per-mesh
    // attachment bone (Phase 4c).
    for (int i = 0; i < top_count; ++i) {
        walk_node(igame->GetTopLevelNode(i), out, bone_map);
    }

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

}  // namespace max2alamo

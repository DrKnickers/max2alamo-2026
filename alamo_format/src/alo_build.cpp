#include "alamo_format/alo_build.h"

#include "alamo_format/chunk_io.h"
#include "alamo_format/collision_tree.h"
#include "alamo_format/shader_table.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace alamo_format {

namespace {

// Fallback vertex-format-name string for 0x10002 when an ExportSubmesh
// leaves `vertex_format_name` empty. Phase 10 plumbed the per-submesh
// field through the walker (issue #75); this default preserves back-
// compat for tests + alo_synth callers that build ExportSubmesh
// objects without going through the walker.
constexpr const char* kDefaultVertexFormatName = "alD3dVertNU2";

// Vertex chunk ID (rev 2 = 144 bytes per vertex). Vanilla content uses
// 0x10007 for >98% of submeshes, including all alD3dVertNU2 chunks
// observed in the corpus, so we standardize on it.
constexpr std::uint32_t kVertexChunkId = 0x10007;
constexpr std::size_t   kBytesPerVertex = 144;  // B4I4 rev 2 layout

// Fixed-size chunks documented in format-notes.md (and discovered the hard
// way by comparing our output to vanilla content in Phase 4c -- some of
// these aren't called out in the spec but are constant across all vanilla
// files in the corpus).
constexpr std::size_t kSkeletonInfoBytes = 128;   // 0x201
constexpr std::size_t kMeshInfoBytes     = 128;   // 0x402
constexpr std::size_t kSubmeshSizesBytes = 128;   // 0x10001
constexpr std::size_t kBoneDataBytes     = 60;    // 0x205 / 0x206

// Material parameter mini-chunk IDs.
constexpr std::uint8_t kParamNameMini  = 1;
constexpr std::uint8_t kParamValueMini = 2;

// 0x601 / 0x602 mini-chunk IDs.
constexpr std::uint8_t kCountConnectionsMini = 1;
constexpr std::uint8_t kCountProxiesMini     = 4;
constexpr std::uint8_t kConnObjectIdxMini    = 2;
constexpr std::uint8_t kConnObjectBoneMini   = 3;

// 0x603 (proxy) mini-chunk IDs, per Mike Lankamp's alamo2max.ms:584+.
// Mini-chunks 7 and 8 are optional -- vanilla content omits them when
// their boolean value is default-false, and Mike's reader treats them
// as a tail of optional types ending in the -1 end-marker.
constexpr std::uint8_t kProxyNameMini             = 5;
constexpr std::uint8_t kProxyBoneMini             = 6;
constexpr std::uint8_t kProxyIsHiddenMini         = 7;  // optional
constexpr std::uint8_t kProxyAltDecStayHiddenMini = 8;  // optional

// 0x1302 light-data payload is exactly 36 bytes:
//   u32 type, 3*f32 color, f32 intensity, f32 atten_end,
//   f32 atten_start, f32 hotspot, f32 falloff.
constexpr std::size_t kLightDataBytes = 36;

// ---- Helpers --------------------------------------------------------------

ChunkNode make_leaf(std::uint32_t id, std::vector<std::uint8_t> payload) {
    ChunkNode n;
    n.id = id;
    n.is_container = false;
    n.payload = std::move(payload);
    return n;
}

ChunkNode make_container(std::uint32_t id, std::vector<ChunkNode> children) {
    ChunkNode n;
    n.id = id;
    n.is_container = true;
    n.children = std::move(children);
    return n;
}

// Append helpers writing into a flat byte vector.
void append_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    const std::size_t off = v.size();
    v.resize(off + 4);
    std::memcpy(v.data() + off, &x, 4);
}
void append_f32(std::vector<std::uint8_t>& v, float x) {
    const std::size_t off = v.size();
    v.resize(off + 4);
    std::memcpy(v.data() + off, &x, 4);
}
void append_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    const std::size_t off = v.size();
    v.resize(off + 2);
    std::memcpy(v.data() + off, &x, 2);
}
void append_zeros(std::vector<std::uint8_t>& v, std::size_t n) {
    v.insert(v.end(), n, std::uint8_t{0});
}
void append_cstring(std::vector<std::uint8_t>& v, const std::string& s) {
    v.insert(v.end(), s.begin(), s.end());
    v.push_back(0);
}

// ---- Skeleton -------------------------------------------------------------

ChunkNode build_skeleton_info(std::uint32_t bone_count) {
    std::vector<std::uint8_t> p;
    p.reserve(kSkeletonInfoBytes);
    append_u32(p, bone_count);
    append_zeros(p, kSkeletonInfoBytes - 4);  // 124 bytes reserved
    return make_leaf(0x201, std::move(p));
}

ChunkNode build_bone_data(const ExportBone& bone) {
    // 0x206 = bone data with billboard. We always emit 0x206 to match
    // the dominant pattern in vanilla content (44653 occurrences vs
    // 18 for 0x205); it costs 4 extra bytes and gains uniformity.
    std::vector<std::uint8_t> p;
    p.reserve(kBoneDataBytes);
    append_u32(p, bone.parent_index);
    append_u32(p, bone.visible ? 1u : 0u);
    append_u32(p, bone.billboard_mode);
    for (float v : bone.matrix) append_f32(p, v);
    return make_leaf(0x206, std::move(p));
}

ChunkNode build_bone(const ExportBone& bone) {
    std::vector<std::uint8_t> name_payload;
    append_cstring(name_payload, bone.name);

    std::vector<ChunkNode> kids;
    kids.push_back(make_leaf(0x203, std::move(name_payload)));
    kids.push_back(build_bone_data(bone));
    return make_container(0x202, std::move(kids));
}

ChunkNode build_skeleton(const std::vector<ExportBone>& bones) {
    std::vector<ChunkNode> kids;
    kids.push_back(build_skeleton_info(static_cast<std::uint32_t>(bones.size())));
    for (const auto& b : bones) kids.push_back(build_bone(b));
    return make_container(0x200, std::move(kids));
}

// ---- Mesh -----------------------------------------------------------------

ChunkNode build_mesh_info(const ExportMesh& mesh) {
    std::vector<std::uint8_t> p;
    p.reserve(kMeshInfoBytes);
    append_u32(p, static_cast<std::uint32_t>(mesh.submeshes.size()));
    // 6 floats: bbox min[3] then max[3]. Confirmed Phase 9.1 against
    // AloViewer source (`src/Assets/Models.cpp:184-190` reads exactly
    // this layout: materialCount, min[3], max[3], unused, isHidden,
    // isCollisionMesh) AND empirically (69/69 vanilla meshes have
    // 0x402 floats match the mesh's vertex AABB within 1e-3 tolerance).
    for (float v : mesh.bbox_min) append_f32(p, v);
    for (float v : mesh.bbox_max) append_f32(p, v);
    append_u32(p, 0);  // unused
    append_u32(p, mesh.is_hidden    ? 1u : 0u);
    append_u32(p, mesh.is_collision ? 1u : 0u);
    append_zeros(p, kMeshInfoBytes - 40);  // 88 bytes reserved
    return make_leaf(0x402, std::move(p));
}

// 0x10105 (TEXTURE param). Mini-chunks: 1=name, 2=value (string).
ChunkNode build_texture_param(const std::string& param_name,
                              const std::string& texture_filename)
{
    std::vector<std::uint8_t> p;
    // mini chunk 1: param name (cstring).
    p.push_back(kParamNameMini);
    p.push_back(static_cast<std::uint8_t>(param_name.size() + 1));
    append_cstring(p, param_name);
    // mini chunk 2: value (cstring).
    p.push_back(kParamValueMini);
    p.push_back(static_cast<std::uint8_t>(texture_filename.size() + 1));
    append_cstring(p, texture_filename);
    return make_leaf(0x10105, std::move(p));
}

// 0x10103 (FLOAT param). Mini-chunks: 1=name, 2=value (1 LE float).
ChunkNode build_float_param(const std::string& param_name, float value) {
    std::vector<std::uint8_t> p;
    p.push_back(kParamNameMini);
    p.push_back(static_cast<std::uint8_t>(param_name.size() + 1));
    append_cstring(p, param_name);
    p.push_back(kParamValueMini);
    p.push_back(4);
    append_f32(p, value);
    return make_leaf(0x10103, std::move(p));
}

// 0x10106 (FLOAT4 param). Mini-chunks: 1=name, 2=value (4 LE floats).
// Vanilla content uses this chunk for both float3 and float4 declarations
// in the .fxh -- float3 values get a trailing 0.0.
ChunkNode build_float4_param(const std::string& param_name,
                             const std::array<float, 4>& value)
{
    std::vector<std::uint8_t> p;
    p.push_back(kParamNameMini);
    p.push_back(static_cast<std::uint8_t>(param_name.size() + 1));
    append_cstring(p, param_name);
    p.push_back(kParamValueMini);
    p.push_back(16);
    for (float v : value) append_f32(p, v);
    return make_leaf(0x10106, std::move(p));
}

// One 144-byte vertex in the B4I4 rev-2 layout. Phase 4 filled pos /
// normal / uv0 from the source mesh; Phase 6b added tangent + binormal;
// Phase 5b now reads bone indices + weights per-vertex from
// ExportVertex itself (previously a submesh-wide parameter). Other
// slots get neutral defaults (uv1..3 zeros, white color, alpha=1).
void append_vertex(std::vector<std::uint8_t>& out, const ExportVertex& v)
{
    // pos (12)
    append_f32(out, v.position[0]);
    append_f32(out, v.position[1]);
    append_f32(out, v.position[2]);
    // normal (12)
    append_f32(out, v.normal[0]);
    append_f32(out, v.normal[1]);
    append_f32(out, v.normal[2]);
    // uv0 (8) -- already V-flipped by the walker
    append_f32(out, v.uv[0]);
    append_f32(out, v.uv[1]);
    // uv1, uv2, uv3 (24) -- zeros
    append_zeros(out, 24);
    // tangent (12)
    append_f32(out, v.tangent[0]);
    append_f32(out, v.tangent[1]);
    append_f32(out, v.tangent[2]);
    // binormal (12) -- handedness sign baked in by the walker
    append_f32(out, v.binormal[0]);
    append_f32(out, v.binormal[1]);
    append_f32(out, v.binormal[2]);
    // color (12) -- white
    append_f32(out, 1.0f);
    append_f32(out, 1.0f);
    append_f32(out, 1.0f);
    // alpha (4) -- 1.0
    append_f32(out, 1.0f);
    // unused 16 (rev 2)
    append_zeros(out, 16);
    // boneIdx[4] (16). Phase 5b: per-vertex slot 0 = dominant bone for
    // skinned meshes; for static/rigid meshes the walker fills all four
    // verts with the same per-mesh bone index. Slots 1..3 are unused
    // pending Phase 5c (multi-bone weighted skinning).
    append_u32(out, v.bone_indices[0]);
    append_u32(out, v.bone_indices[1]);
    append_u32(out, v.bone_indices[2]);
    append_u32(out, v.bone_indices[3]);
    // weight[4] (16)
    append_f32(out, v.weights[0]);
    append_f32(out, v.weights[1]);
    append_f32(out, v.weights[2]);
    append_f32(out, v.weights[3]);
}

ChunkNode build_vertex_chunk(const std::vector<ExportVertex>& verts)
{
    std::vector<std::uint8_t> p;
    p.reserve(verts.size() * kBytesPerVertex);
    for (const auto& v : verts) append_vertex(p, v);
    return make_leaf(kVertexChunkId, std::move(p));
}

ChunkNode build_face_chunk(const std::vector<std::uint32_t>& indices) {
    // 3 uint16 indices per triangle. Indices must fit in 16 bits;
    // Phase 4 emits one vertex per face corner so vertex count <= 65535
    // is the only constraint and is enforced by the caller in build_alo.
    std::vector<std::uint8_t> p;
    p.reserve(indices.size() * 2);
    for (auto i : indices) {
        append_u16(p, static_cast<std::uint16_t>(i));
    }
    return make_leaf(0x10004, std::move(p));
}

// Look up a typed parameter by name in the source ExportMaterial. Returns
// null if the source material did not override that param -- caller falls
// back to the shader-table default.
const MaterialParam* find_param(const ExportMaterial& mat, std::string_view name) {
    for (const auto& p : mat.params) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

// Append one typed parameter chunk according to the shader-table spec.
// Scalar / vector params are always emitted (using the spec default when
// the source material doesn't override). Texture params are only emitted
// when the source material (or back-compat `base_texture` shortcut)
// provides a non-empty filename.
void emit_param_chunk(std::vector<ChunkNode>& kids,
                      const ExportMaterial& mat,
                      const shader_table::ParamSpec& spec)
{
    const MaterialParam* override_p = find_param(mat, spec.name);
    switch (spec.kind) {
        case MaterialParam::Kind::Float: {
            const float v = override_p ? override_p->value4[0] : spec.default_value4[0];
            kids.push_back(build_float_param(std::string(spec.name), v));
            return;
        }
        case MaterialParam::Kind::Float4: {
            std::array<float, 4> v = override_p ? override_p->value4 : spec.default_value4;
            // Vanilla content writes 0 in the 4th slot for float3-declared
            // params (Emissive / Diffuse / Specular / CityColor / etc.) even
            // though the on-disk chunk is still 16 bytes. Max's TYPE_FRGBA
            // hands us AColor with alpha=1; force it to 0 here so we match.
            if (spec.is_float3) {
                v[3] = 0.0f;
            }
            kids.push_back(build_float4_param(std::string(spec.name), v));
            return;
        }
        case MaterialParam::Kind::Texture: {
            std::string fn = override_p ? override_p->texture : std::string{};
            // Back-compat: legacy callers populate ExportMaterial::base_texture
            // directly instead of going through the params list.
            if (fn.empty() && spec.name == "BaseTexture") {
                fn = mat.base_texture;
            }
            if (!fn.empty()) {
                kids.push_back(build_texture_param(std::string(spec.name), fn));
            }
            return;
        }
    }
}

// 0x10100 (Submesh material): shader name + ordered shader-param chunks.
// Crucially, this does NOT contain the geometry -- 0x10000 is a SIBLING of
// 0x10100 inside the parent 0x400, not a child. Mike's MAXScript reader
// (ReadMaterial in alamo2max.ms) confirms this by walking 0x10100's
// children for params via Next() until -1, then reading 0x10000 at the
// next sibling level. Vanilla content (e.g. I_DEATHSTAR_SWITCH.ALO,
// W_SUN00.ALO) follows this layout exactly.
//
// Phase 6c: param emission is now driven by the shader_table so each
// shader receives its canonical ordered param list (with overrides from
// `ExportMaterial::params` when the source material has values). For
// shaders not in the table we fall back to the Phase 4 minimal layout
// (just shader_name + BaseTexture).
ChunkNode build_submesh_material(const ExportSubmesh& sub) {
    std::vector<ChunkNode> kids;
    // 0x10101: shader name.
    std::vector<std::uint8_t> shader_name;
    append_cstring(shader_name, sub.material.shader_name);
    kids.push_back(make_leaf(0x10101, std::move(shader_name)));

    if (shader_table::contains(sub.material.shader_name)) {
        // Known shader: emit exactly the params the table declares, in
        // canonical order. A zero-param entry (alDefault) emits nothing.
        for (const auto& spec : shader_table::params_for(sub.material.shader_name)) {
            emit_param_chunk(kids, sub.material, spec);
        }
    } else if (!sub.material.base_texture.empty()) {
        // Unknown shader: keep the Phase 4 fallback of emitting just a
        // BaseTexture entry if one is provided.
        kids.push_back(build_texture_param("BaseTexture", sub.material.base_texture));
    }

    return make_container(0x10100, std::move(kids));
}

// Phase 9.2: build the 0x1200 collision-tree subtree from a submesh's
// triangles + parent mesh AABB. Returns nullopt-equivalent (empty
// ChunkNode discardable by the caller) iff the submesh has no usable
// triangles. The caller invokes this only for collision meshes.
ChunkNode build_collision_tree_for_submesh(const ExportSubmesh& sub,
                                           const ExportMesh&    parent)
{
    std::vector<CollisionTriangle> tris;
    const std::size_t n_tris = sub.indices.size() / 3;
    tris.reserve(n_tris);
    for (std::size_t t = 0; t < n_tris; ++t) {
        const std::uint32_t i0 = sub.indices[t * 3 + 0];
        const std::uint32_t i1 = sub.indices[t * 3 + 1];
        const std::uint32_t i2 = sub.indices[t * 3 + 2];
        if (i0 >= sub.vertices.size() ||
            i1 >= sub.vertices.size() ||
            i2 >= sub.vertices.size()) continue;
        const auto& p0 = sub.vertices[i0].position;
        const auto& p1 = sub.vertices[i1].position;
        const auto& p2 = sub.vertices[i2].position;
        CollisionTriangle ct;
        ct.face_index = static_cast<std::uint16_t>(t);
        for (int a = 0; a < 3; ++a) {
            ct.aabb_min[a] = std::min({p0[a], p1[a], p2[a]});
            ct.aabb_max[a] = std::max({p0[a], p1[a], p2[a]});
            ct.centroid[a] = (p0[a] + p1[a] + p2[a]) * (1.0f / 3.0f);
        }
        tris.push_back(ct);
    }
    return build_collision_tree(tris, parent.bbox_min, parent.bbox_max);
}

// 0x10000 (Submesh data): sizes + format + vertices + faces. Sibling of
// 0x10100 above. Each vertex carries its own bone bindings (Phase 5b);
// the walker fills them in for both static (rigid attachment to per-mesh
// bone) and skinned (dominant bone per vertex) cases.
//
// Phase 9.2: if the parent mesh's is_collision flag is set, append a
// 0x1200 collision-tree child at the end of the geometry block (matches
// vanilla layout per Petrolution spec).
ChunkNode build_submesh_geometry(const ExportSubmesh& sub,
                                 const ExportMesh*    parent_for_collision)
{
    std::vector<ChunkNode> kids;

    // 0x10001: fixed 128 bytes -- u32 vertexCount, u32 faceCount, then 120
    // reserved zero bytes. Mike's reader only consumes the first 8 bytes
    // and skips the rest, but the chunk size is constant in vanilla
    // content and AloViewer rejects shorter ones.
    {
        std::vector<std::uint8_t> p;
        p.reserve(kSubmeshSizesBytes);
        append_u32(p, static_cast<std::uint32_t>(sub.vertices.size()));
        append_u32(p, static_cast<std::uint32_t>(sub.indices.size() / 3));
        append_zeros(p, kSubmeshSizesBytes - 8);
        kids.push_back(make_leaf(0x10001, std::move(p)));
    }

    // 0x10002: vertex format name as cstring. Use the per-submesh
    // value when the caller populated it; fall back to the basic
    // mesh format for back-compat with pre-Phase-10 callers.
    {
        std::vector<std::uint8_t> p;
        const std::string vfmt = sub.vertex_format_name.empty()
                                   ? std::string(kDefaultVertexFormatName)
                                   : sub.vertex_format_name;
        append_cstring(p, vfmt);
        kids.push_back(make_leaf(0x10002, std::move(p)));
    }

    // 0x10007: vertex data (144 B/vertex).
    kids.push_back(build_vertex_chunk(sub.vertices));

    // 0x10004: face indices (3 x uint16 per triangle).
    kids.push_back(build_face_chunk(sub.indices));

    // 0x1200: collision tree (Phase 9.2). Only attached when the parent
    // mesh is flagged as a collision mesh. Without one, the engine builds
    // a runtime BVH at load time -- so this is an optimization, not a
    // correctness fix. Vanilla content always carries it (142/142 in a
    // 100-file FoC sample).
    if (parent_for_collision && parent_for_collision->is_collision) {
        kids.push_back(build_collision_tree_for_submesh(sub, *parent_for_collision));
    }

    return make_container(0x10000, std::move(kids));
}

ChunkNode build_mesh(const ExportMesh& mesh) {
    std::vector<ChunkNode> kids;
    // 0x401: name.
    {
        std::vector<std::uint8_t> p;
        append_cstring(p, mesh.name);
        kids.push_back(make_leaf(0x401, std::move(p)));
    }
    // 0x402: metadata.
    kids.push_back(build_mesh_info(mesh));
    // Per-submesh: 0x10100 (material) and 0x10000 (geometry) appear as
    // SIBLINGS at this level, alternating per submesh. NOT nested.
    for (const auto& sub : mesh.submeshes) {
        kids.push_back(build_submesh_material(sub));
        kids.push_back(build_submesh_geometry(sub, &mesh));
    }
    return make_container(0x400, std::move(kids));
}

// ---- Lights (Phase 7a) ----------------------------------------------------

ChunkNode build_light_name(const std::string& name) {
    std::vector<std::uint8_t> p;
    append_cstring(p, name);
    return make_leaf(0x1301, std::move(p));
}

ChunkNode build_light_data(const ExportLight& light) {
    std::vector<std::uint8_t> p;
    p.reserve(kLightDataBytes);
    append_u32(p, static_cast<std::uint32_t>(light.type));
    append_f32(p, light.color[0]);
    append_f32(p, light.color[1]);
    append_f32(p, light.color[2]);
    append_f32(p, light.intensity);
    append_f32(p, light.atten_end);    // farAttenuationEnd  (Mike: GetFloat #4)
    append_f32(p, light.atten_start);  // farAttenuationStart (Mike: GetFloat #5)
    append_f32(p, light.hotspot);
    append_f32(p, light.falloff);
    return make_leaf(0x1302, std::move(p));
}

ChunkNode build_light(const ExportLight& light) {
    std::vector<ChunkNode> kids;
    kids.push_back(build_light_name(light.name));
    kids.push_back(build_light_data(light));
    return make_container(0x1300, std::move(kids));
}

// ---- Proxies (Phase 7a) ---------------------------------------------------

// Append a mini-chunk: 1-byte type + 1-byte size + payload bytes.
void append_mini_u32(std::vector<std::uint8_t>& p, std::uint8_t type, std::uint32_t v) {
    p.push_back(type);
    p.push_back(4);
    append_u32(p, v);
}

ChunkNode build_proxy(const ExportProxy& proxy) {
    std::vector<std::uint8_t> p;
    // Required: name + bone.
    p.push_back(kProxyNameMini);
    p.push_back(static_cast<std::uint8_t>(proxy.name.size() + 1));
    append_cstring(p, proxy.name);
    append_mini_u32(p, kProxyBoneMini, proxy.bone_index);
    // Optional: is_hidden, alt_decrease_stay_hidden. Vanilla content
    // omits both when default-false; Mike's reader treats each as an
    // optional run before the -1 end marker.
    if (proxy.is_hidden) {
        append_mini_u32(p, kProxyIsHiddenMini, 1u);
    }
    if (proxy.alt_decrease_stay_hidden) {
        append_mini_u32(p, kProxyAltDecStayHiddenMini, 1u);
    }
    return make_leaf(0x603, std::move(p));
}

// ---- Connections ----------------------------------------------------------

ChunkNode build_connections(const ExportScene& scene) {
    std::vector<ChunkNode> kids;

    // 0x601: counts. nConnections = meshes + lights (each is a
    // "connection object" with its own 0x602 entry). nProxies =
    // scene.proxies.size(). Phase 7a is the first time these can
    // be non-zero or non-meshes; Phase 4 hard-coded both as
    // (meshes.size(), 0).
    const std::uint32_t n_connections =
        static_cast<std::uint32_t>(scene.meshes.size() + scene.lights.size());
    const std::uint32_t n_proxies =
        static_cast<std::uint32_t>(scene.proxies.size());
    {
        std::vector<std::uint8_t> p;
        append_mini_u32(p, kCountConnectionsMini, n_connections);
        append_mini_u32(p, kCountProxiesMini,     n_proxies);
        kids.push_back(make_leaf(0x601, std::move(p)));
    }

    // 0x602: per-object connection. Object index = position in
    // (meshes ++ lights), monotonically increasing. Bone index
    // comes from each object's bone_index (set by the walker to
    // the per-object attachment bone, never 0=Root for static
    // meshes; per Phase 5b skinned meshes connect to Root=0).
    std::uint32_t obj_idx = 0;
    for (const auto& m : scene.meshes) {
        std::vector<std::uint8_t> p;
        append_mini_u32(p, kConnObjectIdxMini,  obj_idx++);
        append_mini_u32(p, kConnObjectBoneMini, m.bone_index);
        kids.push_back(make_leaf(0x602, std::move(p)));
    }
    for (const auto& l : scene.lights) {
        std::vector<std::uint8_t> p;
        append_mini_u32(p, kConnObjectIdxMini,  obj_idx++);
        append_mini_u32(p, kConnObjectBoneMini, l.bone_index);
        kids.push_back(make_leaf(0x602, std::move(p)));
    }

    // 0x603: proxies (tail of 0x600).
    for (const auto& proxy : scene.proxies) {
        kids.push_back(build_proxy(proxy));
    }

    return make_container(0x600, std::move(kids));
}

}  // namespace

std::vector<ChunkNode> build_alo(const ExportScene& scene) {
    std::vector<ChunkNode> top;
    top.push_back(build_skeleton(scene.bones));
    for (const auto& mesh : scene.meshes) {
        top.push_back(build_mesh(mesh));
    }
    // Phase 7a: lights are top-level siblings of meshes, between
    // 0x400 meshes and 0x600 connections (per vanilla
    // EB_ICC_LANDINGPAD.ALO inspection; Mike's reader reads them
    // in this slot too).
    for (const auto& light : scene.lights) {
        top.push_back(build_light(light));
    }
    top.push_back(build_connections(scene));
    return top;
}

}  // namespace alamo_format

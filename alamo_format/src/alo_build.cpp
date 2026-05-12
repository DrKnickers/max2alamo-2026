#include "alamo_format/alo_build.h"

#include "alamo_format/chunk_io.h"
#include "alamo_format/shader_table.h"

#include <cstring>

namespace alamo_format {

namespace {

// Vertex format string emitted in 0x10002. Phase 4 only emits the
// non-skinned, no-tangent layout. Other layouts come online as later
// phases need them.
constexpr const char* kVertexFormatName = "alD3dVertNU2";

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
    // 6 floats: bbox min[3] then max[3]. Confirmed empirically against
    // round-trip via Mike's importer in Phase 4c (TODO: validate).
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

// One 144-byte vertex in the B4I4 rev-2 layout. Phase 4 fills pos / normal
// / uv0 / tangent / binormal from the source mesh; the rest get neutral
// defaults (zero UVs, white color, alpha=1, bound to `bone_index` with
// weight 1). Phase 6b: tangent + binormal are now populated by the walker
// (previously zero, which broke bump-mapped shaders at runtime).
//
// `bone_index` is the file-side bone index (same scheme used by the
// 0x602 connection chunk minus 1). For Phase 4 every vertex of a given
// mesh shares the same bone -- no real skinning yet.
void append_vertex(std::vector<std::uint8_t>& out, const ExportVertex& v,
                   std::uint32_t bone_index)
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
    // boneIdx[4] (16) -- bone_index in slot 0, then padding zeros.
    // Slot 0 carries the only meaningful bone for Phase 4; multi-bone
    // skinning fills slots 1..3 in Phase 5.
    append_u32(out, bone_index);
    for (int i = 1; i < 4; ++i) append_u32(out, 0u);
    // weight[4] (16) -- [1, 0, 0, 0]
    append_f32(out, 1.0f);
    append_f32(out, 0.0f);
    append_f32(out, 0.0f);
    append_f32(out, 0.0f);
}

ChunkNode build_vertex_chunk(const std::vector<ExportVertex>& verts,
                             std::uint32_t bone_index)
{
    std::vector<std::uint8_t> p;
    p.reserve(verts.size() * kBytesPerVertex);
    for (const auto& v : verts) append_vertex(p, v, bone_index);
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
            const std::array<float, 4>& v = override_p ? override_p->value4 : spec.default_value4;
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

// 0x10000 (Submesh data): sizes + format + vertices + faces. Sibling of
// 0x10100 above. `bone_index` is the file-side bone index that every
// vertex's boneIdx[0] will point at.
ChunkNode build_submesh_geometry(const ExportSubmesh& sub, std::uint32_t bone_index)
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

    // 0x10002: vertex format name as cstring.
    {
        std::vector<std::uint8_t> p;
        append_cstring(p, kVertexFormatName);
        kids.push_back(make_leaf(0x10002, std::move(p)));
    }

    // 0x10007: vertex data (144 B/vertex).
    kids.push_back(build_vertex_chunk(sub.vertices, bone_index));

    // 0x10004: face indices (3 x uint16 per triangle).
    kids.push_back(build_face_chunk(sub.indices));

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
        kids.push_back(build_submesh_geometry(sub, mesh.bone_index));
    }
    return make_container(0x400, std::move(kids));
}

// ---- Connections ----------------------------------------------------------

ChunkNode build_connections(const ExportScene& scene) {
    std::vector<ChunkNode> kids;

    // 0x601: counts.
    {
        std::vector<std::uint8_t> p;
        // mini 1: nConnections.
        p.push_back(kCountConnectionsMini);
        p.push_back(4);
        append_u32(p, static_cast<std::uint32_t>(scene.meshes.size()));
        // mini 4: nProxies = 0 (Phase 4 emits no proxies).
        p.push_back(kCountProxiesMini);
        p.push_back(4);
        append_u32(p, 0u);
        kids.push_back(make_leaf(0x601, std::move(p)));
    }

    // 0x602: per-object connection. Object index = position in
    // (meshes ++ lights). Phase 4 has no lights, so it's just the
    // mesh index. Bone index comes from mesh.bone_index (set by the
    // walker to the per-mesh attachment bone, never 0=Root).
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        std::vector<std::uint8_t> p;
        // mini 2: object index.
        p.push_back(kConnObjectIdxMini);
        p.push_back(4);
        append_u32(p, static_cast<std::uint32_t>(i));
        // mini 3: bone index.
        p.push_back(kConnObjectBoneMini);
        p.push_back(4);
        append_u32(p, scene.meshes[i].bone_index);
        kids.push_back(make_leaf(0x602, std::move(p)));
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
    top.push_back(build_connections(scene));
    return top;
}

}  // namespace alamo_format

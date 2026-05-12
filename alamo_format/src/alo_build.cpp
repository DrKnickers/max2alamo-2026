#include "alamo_format/alo_build.h"

#include "alamo_format/chunk_io.h"

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

// Fixed-size chunks documented in format-notes.md.
constexpr std::size_t kSkeletonInfoBytes = 128;   // 0x201
constexpr std::size_t kMeshInfoBytes     = 128;   // 0x402
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

// 0x10005 (TEXTURE param). Mini-chunks: 1=name, 2=value (string).
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

// One 144-byte vertex in the B4I4 rev-2 layout. Phase 4 fills only the
// fields the format name references (pos / normal / uv0); the rest get
// neutral defaults (zero UVs, zero tangent/binormal, white color, alpha=1,
// bound to bone 0 with weight 1).
void append_vertex(std::vector<std::uint8_t>& out, const ExportVertex& v) {
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
    // tangent (12) -- zero
    append_zeros(out, 12);
    // binormal (12) -- zero
    append_zeros(out, 12);
    // color (12) -- white
    append_f32(out, 1.0f);
    append_f32(out, 1.0f);
    append_f32(out, 1.0f);
    // alpha (4) -- 1.0
    append_f32(out, 1.0f);
    // unused 16 (rev 2)
    append_zeros(out, 16);
    // boneIdx[4] (16) -- bone 0 (Root)
    for (int i = 0; i < 4; ++i) append_u32(out, 0u);
    // weight[4] (16) -- [1, 0, 0, 0]
    append_f32(out, 1.0f);
    append_f32(out, 0.0f);
    append_f32(out, 0.0f);
    append_f32(out, 0.0f);
}

ChunkNode build_vertex_chunk(const std::vector<ExportVertex>& verts) {
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

ChunkNode build_geometry(const ExportSubmesh& sub) {
    std::vector<ChunkNode> kids;

    // 0x10001: u32 vertexCount, u32 faceCount.
    {
        std::vector<std::uint8_t> p;
        append_u32(p, static_cast<std::uint32_t>(sub.vertices.size()));
        append_u32(p, static_cast<std::uint32_t>(sub.indices.size() / 3));
        kids.push_back(make_leaf(0x10001, std::move(p)));
    }

    // 0x10002: vertex format name as cstring.
    {
        std::vector<std::uint8_t> p;
        append_cstring(p, kVertexFormatName);
        kids.push_back(make_leaf(0x10002, std::move(p)));
    }

    // 0x10007: vertex data (144 B/vertex).
    kids.push_back(build_vertex_chunk(sub.vertices));

    // 0x10004: face indices (3 x uint16 per triangle).
    kids.push_back(build_face_chunk(sub.indices));

    return make_container(0x10000, std::move(kids));
}

ChunkNode build_submesh(const ExportSubmesh& sub) {
    std::vector<ChunkNode> kids;
    // 0x10101: shader name.
    std::vector<std::uint8_t> shader_name;
    append_cstring(shader_name, sub.material.shader_name);
    kids.push_back(make_leaf(0x10101, std::move(shader_name)));

    // Optional 0x10105 BaseTexture if a texture was extracted.
    if (!sub.material.base_texture.empty()) {
        kids.push_back(build_texture_param("BaseTexture", sub.material.base_texture));
    }

    // 0x10000 geometry container.
    kids.push_back(build_geometry(sub));

    return make_container(0x10100, std::move(kids));
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
    // Per-submesh 0x10100.
    for (const auto& sub : mesh.submeshes) {
        kids.push_back(build_submesh(sub));
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
    // mesh index. Bone index = 0 (Root) for every Phase 4 mesh.
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        std::vector<std::uint8_t> p;
        // mini 2: object index.
        p.push_back(kConnObjectIdxMini);
        p.push_back(4);
        append_u32(p, static_cast<std::uint32_t>(i));
        // mini 3: bone index.
        p.push_back(kConnObjectBoneMini);
        p.push_back(4);
        append_u32(p, 0u);
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

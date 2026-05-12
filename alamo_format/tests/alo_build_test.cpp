#include <catch2/catch_test_macros.hpp>

#include "alamo_format/alo_build.h"
#include "alamo_format/chunk_tree.h"
#include "alamo_format/chunk_io.h"

#include <cstring>

using namespace alamo_format;

namespace {

ExportScene minimal_cube_scene()
{
    ExportScene s = ExportScene::with_root_bone();

    // Vanilla static-prop layout requires a per-mesh attachment bone in
    // addition to the synthetic Root, otherwise Mike's importer crashes
    // (he deletes Root on import; vertex bone references then dangle).
    ExportBone mesh_bone;
    mesh_bone.name         = "Cube";
    mesh_bone.parent_index = 0;
    s.bones.push_back(mesh_bone);

    ExportMesh mesh;
    mesh.name       = "Cube";
    mesh.bone_index = 1;   // points at the bone we just added
    mesh.bbox_min   = { -1.f, -1.f, -1.f };
    mesh.bbox_max   = {  1.f,  1.f,  1.f };

    ExportSubmesh sub;
    sub.material.shader_name  = "MeshAlpha.fx";
    sub.material.base_texture = "tex_cube.tga";
    // 12 faces (cube), 3 verts per face = 36 vertices
    for (int i = 0; i < 36; ++i) {
        ExportVertex v;
        v.position = { float(i % 3 - 1), float((i / 3) % 3 - 1), 0.f };
        v.normal   = { 0.f, 0.f, 1.f };
        v.uv       = { 0.5f, 0.5f };
        sub.vertices.push_back(v);
        sub.indices.push_back(static_cast<std::uint32_t>(i));
    }

    mesh.submeshes.push_back(std::move(sub));
    s.meshes.push_back(std::move(mesh));
    return s;
}

}  // namespace

TEST_CASE("build_alo emits skeleton + mesh + connections at top level") {
    auto tree = build_alo(minimal_cube_scene());
    REQUIRE(tree.size() == 3);
    REQUIRE(tree[0].id == 0x200);  REQUIRE(tree[0].is_container);
    REQUIRE(tree[1].id == 0x400);  REQUIRE(tree[1].is_container);
    REQUIRE(tree[2].id == 0x600);  REQUIRE(tree[2].is_container);
}

TEST_CASE("Skeleton info is exactly 128 bytes; first u32 is bone count") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& info = tree[0].children[0];
    REQUIRE(info.id == 0x201);
    REQUIRE_FALSE(info.is_container);
    REQUIRE(info.payload.size() == 128);
    std::uint32_t bone_count = 0;
    std::memcpy(&bone_count, info.payload.data(), 4);
    REQUIRE(bone_count == 2);   // Root + per-mesh attachment bone
    for (std::size_t i = 4; i < 128; ++i) {
        REQUIRE(info.payload[i] == 0);
    }
}

TEST_CASE("Default identity bone matrix on disk is exactly the column-major identity Mike's reader expects") {
    auto tree = build_alo(minimal_cube_scene());
    // Root bone is tree[0].children[1]. Its 0x206 leaf is at children[1].
    // The 60-byte payload: parent(4) + visible(4) + billboard(4) + 12 floats.
    const ChunkNode& bone_data = tree[0].children[1].children[1];
    REQUIRE(bone_data.id == 0x206);
    REQUIRE(bone_data.payload.size() == 60);

    // Read the 12 floats at offset 12.
    std::array<float, 12> m;
    for (std::size_t i = 0; i < 12; ++i) {
        std::memcpy(&m[i], bone_data.payload.data() + 12 + i * 4, 4);
    }

    // Mike's reader does:
    //   bone.transform = Matrix3 [c1,c5,c9] [c2,c6,c10] [c3,c7,c11] [c4,c8,c12]
    // For identity that requires:
    //   c1=1 c2=0 c3=0 c4=0  c5=0 c6=1 c7=0 c8=0  c9=0 c10=0 c11=1 c12=0
    // (c is 1-indexed in MAXScript; our array is 0-indexed so c[N] == m[N-1].)
    REQUIRE(m[0]  == 1.f); REQUIRE(m[1]  == 0.f); REQUIRE(m[2]  == 0.f); REQUIRE(m[3]  == 0.f);
    REQUIRE(m[4]  == 0.f); REQUIRE(m[5]  == 1.f); REQUIRE(m[6]  == 0.f); REQUIRE(m[7]  == 0.f);
    REQUIRE(m[8]  == 0.f); REQUIRE(m[9]  == 0.f); REQUIRE(m[10] == 1.f); REQUIRE(m[11] == 0.f);
}

TEST_CASE("Skeleton has Root + one named per-mesh bone") {
    auto tree = build_alo(minimal_cube_scene());
    // children: 0x201 info, 0x202 Root, 0x202 Cube
    REQUIRE(tree[0].children.size() == 3);

    // Root bone
    const ChunkNode& root = tree[0].children[1];
    REQUIRE(root.id == 0x202);
    REQUIRE(root.children[0].id == 0x203);
    REQUIRE(root.children[0].payload == std::vector<std::uint8_t>{'R', 'o', 'o', 't', 0});
    REQUIRE(root.children[1].id == 0x206);
    REQUIRE(root.children[1].payload.size() == 60);

    // Per-mesh bone
    const ChunkNode& mb = tree[0].children[2];
    REQUIRE(mb.id == 0x202);
    REQUIRE(mb.children[0].id == 0x203);
    REQUIRE(mb.children[0].payload == std::vector<std::uint8_t>{'C', 'u', 'b', 'e', 0});
    REQUIRE(mb.children[1].id == 0x206);
    // Per-mesh bone has parent = 0 (Root); verify the first u32 of bone data.
    std::uint32_t parent = 0;
    std::memcpy(&parent, mb.children[1].payload.data(), 4);
    REQUIRE(parent == 0u);
}

TEST_CASE("Mesh info is exactly 128 bytes; first u32 is material count") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& mesh = tree[1];
    // children: 0x401 name, 0x402 info, 0x10100 material, 0x10000 geometry
    // (0x10100 and 0x10000 are SIBLINGS, not nested -- per vanilla layout)
    REQUIRE(mesh.children.size() == 4);
    const ChunkNode& info = mesh.children[1];
    REQUIRE(info.id == 0x402);
    REQUIRE(info.payload.size() == 128);
    std::uint32_t mat_count = 0;
    std::memcpy(&mat_count, info.payload.data(), 4);
    REQUIRE(mat_count == 1);
}

TEST_CASE("0x10100 emits shader name + per-shader param chunks per vanilla order") {
    // MeshAlpha.fx in the shader_table maps to:
    //   Emissive (FLOAT4) / Diffuse (FLOAT4) / Specular (FLOAT4) /
    //   Shininess (FLOAT) / BaseTexture (TEXTURE)
    // Plus the 0x10101 shader-name header at the start = 6 child chunks.
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& mat = tree[1].children[2];
    REQUIRE(mat.id == 0x10100);
    REQUIRE(mat.children.size() == 6);
    REQUIRE(mat.children[0].id == 0x10101);  // shader name
    REQUIRE(mat.children[1].id == 0x10106);  // Emissive  (FLOAT4)
    REQUIRE(mat.children[2].id == 0x10106);  // Diffuse   (FLOAT4)
    REQUIRE(mat.children[3].id == 0x10106);  // Specular  (FLOAT4)
    REQUIRE(mat.children[4].id == 0x10103);  // Shininess (FLOAT)
    REQUIRE(mat.children[5].id == 0x10105);  // BaseTexture (TEXTURE)
}

TEST_CASE("0x10106 FLOAT4 chunk layout matches vanilla (name mini + value mini)") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& mat = tree[1].children[2];
    const ChunkNode& diffuse = mat.children[2];
    REQUIRE(diffuse.id == 0x10106);
    // Mini-chunk 1: type=1 (name), size=len+1, then NUL-terminated cstring
    REQUIRE(diffuse.payload.at(0) == 1);            // kParamNameMini
    REQUIRE(diffuse.payload.at(1) == 8);            // "Diffuse\0" = 8 bytes
    REQUIRE(std::string(reinterpret_cast<const char*>(diffuse.payload.data() + 2),
                        7) == "Diffuse");
    REQUIRE(diffuse.payload.at(9) == 0);            // NUL terminator
    // Mini-chunk 2: type=2 (value), size=16, then 4 LE floats
    REQUIRE(diffuse.payload.at(10) == 2);           // kParamValueMini
    REQUIRE(diffuse.payload.at(11) == 16);
    REQUIRE(diffuse.payload.size() == 12 + 16);
    float values[4];
    std::memcpy(values, diffuse.payload.data() + 12, 16);
    // MeshAlpha default Diffuse = (1, 1, 1, 1)
    REQUIRE(values[0] == 1.f); REQUIRE(values[1] == 1.f);
    REQUIRE(values[2] == 1.f); REQUIRE(values[3] == 1.f);
}

TEST_CASE("0x10103 FLOAT chunk layout matches vanilla (name mini + 4-byte value)") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& mat = tree[1].children[2];
    const ChunkNode& shininess = mat.children[4];
    REQUIRE(shininess.id == 0x10103);
    REQUIRE(shininess.payload.at(0) == 1);                                    // name mini
    REQUIRE(shininess.payload.at(1) == 10);                                   // "Shininess\0"
    REQUIRE(shininess.payload.at(12) == 2);                                   // value mini
    REQUIRE(shininess.payload.at(13) == 4);
    REQUIRE(shininess.payload.size() == 14 + 4);
    float v = 0.f;
    std::memcpy(&v, shininess.payload.data() + 14, 4);
    REQUIRE(v == 32.f);  // MeshAlpha default Shininess
}

TEST_CASE("ExportMaterial::params overrides shader-table defaults") {
    ExportScene s = ExportScene::with_root_bone();
    ExportBone b; b.name = "Cube"; b.parent_index = 0; s.bones.push_back(b);
    ExportMesh m;  m.name = "Cube"; m.bone_index = 1;
    ExportSubmesh sub;
    sub.material.shader_name = "MeshAlpha.fx";
    // Override Diffuse only; Emissive / Specular / Shininess fall back
    // to the shader-table defaults.
    MaterialParam p; p.name = "Diffuse"; p.kind = MaterialParam::Kind::Float4;
    p.value4 = { 0.2f, 0.4f, 0.6f, 0.8f };
    sub.material.params.push_back(p);
    for (int i = 0; i < 36; ++i) {
        ExportVertex v;
        v.position = { float(i), 0.f, 0.f }; v.normal = { 0, 0, 1 }; v.uv = { 0, 0 };
        sub.vertices.push_back(v);
        sub.indices.push_back(static_cast<std::uint32_t>(i));
    }
    m.submeshes.push_back(std::move(sub));
    s.meshes.push_back(std::move(m));

    auto tree = build_alo(s);
    const ChunkNode& mat = tree[1].children[2];
    const ChunkNode& diffuse = mat.children[2];
    REQUIRE(diffuse.id == 0x10106);
    float v[4];
    std::memcpy(v, diffuse.payload.data() + 12, 16);
    REQUIRE(v[0] == 0.2f);
    REQUIRE(v[1] == 0.4f);
    REQUIRE(v[2] == 0.6f);
    REQUIRE(v[3] == 0.8f);
}

TEST_CASE("alDefault shader emits no params (empty entry in shader_table)") {
    ExportScene s = ExportScene::with_root_bone();
    ExportBone b; b.name = "M"; b.parent_index = 0; s.bones.push_back(b);
    ExportMesh m; m.name = "M"; m.bone_index = 1;
    ExportSubmesh sub;
    sub.material.shader_name = "alDefault.fx";
    sub.material.base_texture = "ignored.tga";  // not in alDefault's spec
    for (int i = 0; i < 36; ++i) {
        ExportVertex v;
        v.position = { 0, 0, 0 }; v.normal = { 0, 0, 1 }; v.uv = { 0, 0 };
        sub.vertices.push_back(v);
        sub.indices.push_back(static_cast<std::uint32_t>(i));
    }
    m.submeshes.push_back(std::move(sub));
    s.meshes.push_back(std::move(m));
    auto tree = build_alo(s);
    const ChunkNode& mat = tree[1].children[2];
    REQUIRE(mat.id == 0x10100);
    REQUIRE(mat.children.size() == 1);             // shader name only
    REQUIRE(mat.children[0].id == 0x10101);
}

TEST_CASE("Unknown shader falls back to Phase 4 layout (shader name + BaseTexture if any)") {
    ExportScene s = ExportScene::with_root_bone();
    ExportBone b; b.name = "M"; b.parent_index = 0; s.bones.push_back(b);
    ExportMesh m; m.name = "M"; m.bone_index = 1;
    ExportSubmesh sub;
    sub.material.shader_name  = "SomeUnknownShader.fx";
    sub.material.base_texture = "tex.tga";
    for (int i = 0; i < 36; ++i) {
        ExportVertex v;
        v.position = { 0, 0, 0 }; v.normal = { 0, 0, 1 }; v.uv = { 0, 0 };
        sub.vertices.push_back(v);
        sub.indices.push_back(static_cast<std::uint32_t>(i));
    }
    m.submeshes.push_back(std::move(sub));
    s.meshes.push_back(std::move(m));
    auto tree = build_alo(s);
    const ChunkNode& mat = tree[1].children[2];
    REQUIRE(mat.children.size() == 2);
    REQUIRE(mat.children[0].id == 0x10101);
    REQUIRE(mat.children[1].id == 0x10105);
}

TEST_CASE("0x10000 geometry is a SIBLING of 0x10100 inside 0x400") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& geom = tree[1].children[3];
    REQUIRE(geom.id == 0x10000);
    REQUIRE(geom.is_container);
    // children: 0x10001 sizes, 0x10002 format name, 0x10007 verts, 0x10004 faces
    REQUIRE(geom.children.size() == 4);
}

TEST_CASE("0x10001 sizes chunk is exactly 128 bytes") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& geom = tree[1].children[3];
    const ChunkNode& sizes = geom.children[0];
    REQUIRE(sizes.id == 0x10001);
    REQUIRE(sizes.payload.size() == 128);
    std::uint32_t verts = 0, faces = 0;
    std::memcpy(&verts, sizes.payload.data() + 0, 4);
    std::memcpy(&faces, sizes.payload.data() + 4, 4);
    REQUIRE(verts == 36u);
    REQUIRE(faces == 12u);
    // Remaining 120 bytes must be zero.
    for (std::size_t i = 8; i < 128; ++i) REQUIRE(sizes.payload[i] == 0);
}

TEST_CASE("Vertex chunk uses 0x10007 with 144 bytes per vertex") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& geom = tree[1].children[3];
    REQUIRE(geom.children[2].id == 0x10007);
    REQUIRE(geom.children[2].payload.size() == 36u * 144u);
}

TEST_CASE("Face chunk encodes 3 uint16 indices per triangle") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& geom = tree[1].children[3];
    const ChunkNode& faces = geom.children[3];
    REQUIRE(faces.id == 0x10004);
    REQUIRE(faces.payload.size() == 36u * 2u);
}

TEST_CASE("Connections bind each mesh to its per-mesh attachment bone") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& conns = tree[2];
    REQUIRE(conns.id == 0x600);
    REQUIRE(conns.children[0].id == 0x601);
    REQUIRE(conns.children.size() == 2);  // 0x601 counts + 1 x 0x602

    const ChunkNode& obj = conns.children[1];
    REQUIRE(obj.id == 0x602);
    REQUIRE(obj.payload[0] == 2);  // mini ID (object index)
    REQUIRE(obj.payload[1] == 4);
    std::uint32_t obj_idx = 0;
    std::memcpy(&obj_idx, obj.payload.data() + 2, 4);
    REQUIRE(obj_idx == 0u);
    REQUIRE(obj.payload[6] == 3);  // mini ID (bone index)
    REQUIRE(obj.payload[7] == 4);
    std::uint32_t bone_idx = 0;
    std::memcpy(&bone_idx, obj.payload.data() + 8, 4);
    REQUIRE(bone_idx == 1u);   // per-mesh bone, NOT Root (which would be 0)
}

TEST_CASE("Vertex boneIdx[0] points at the mesh's per-mesh bone, not Root") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& geom  = tree[1].children[3];
    const ChunkNode& verts = geom.children[2];
    REQUIRE(verts.id == 0x10007);
    // boneIdx[4] sits at offset 112 in each 144-byte vertex (rev 2 layout).
    // Read the first vertex's first bone slot.
    std::uint32_t bone0 = 0;
    std::memcpy(&bone0, verts.payload.data() + 112, 4);
    REQUIRE(bone0 == 1u);   // attached to per-mesh bone
}

TEST_CASE("build_alo + write_chunk_tree + read_chunk_tree round-trips") {
    auto orig = build_alo(minimal_cube_scene());
    auto bytes = write_chunk_tree(orig);
    auto reparsed = read_chunk_tree(bytes.data(), bytes.size());

    // Top-level shape preserved.
    REQUIRE(reparsed.size() == orig.size());
    for (std::size_t i = 0; i < orig.size(); ++i) {
        REQUIRE(reparsed[i].id == orig[i].id);
        REQUIRE(reparsed[i].is_container == orig[i].is_container);
    }

    // Re-write the reparsed tree and confirm byte-identical.
    auto bytes2 = write_chunk_tree(reparsed);
    REQUIRE(bytes2 == bytes);
}

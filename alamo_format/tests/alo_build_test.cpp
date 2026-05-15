#include <functional>
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
    // 12 faces (cube), 3 verts per face = 36 vertices.
    // Per-vertex bone binding for a static prop: every vertex rigidly
    // attached to the per-mesh attachment bone (index 1) with weight 1.
    for (int i = 0; i < 36; ++i) {
        ExportVertex v;
        v.position = { float(i % 3 - 1), float((i / 3) % 3 - 1), 0.f };
        v.normal   = { 0.f, 0.f, 1.f };
        v.uv       = { 0.5f, 0.5f };
        v.bone_indices = { mesh.bone_index, 0u, 0u, 0u };
        v.weights      = { 1.f, 0.f, 0.f, 0.f };
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

TEST_CASE("Float3-declared params write 0 in the 4th slot (vanilla convention)") {
    // MeshAlpha's `Specular` is declared `float3` in PG's Alpha.fxh. Vanilla
    // .alo files encode it as a 16-byte FLOAT4 chunk with alpha=0. Max's
    // TYPE_FRGBA would hand us alpha=1 -- the writer zeroes it via the
    // ParamSpec::is_float3 flag.
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& mat = tree[1].children[2];
    const ChunkNode& specular = mat.children[3];  // Emissive, Diffuse, Specular, ...
    REQUIRE(specular.id == 0x10106);
    float values[4];
    std::memcpy(values, specular.payload.data() + specular.payload.size() - 16, 16);
    REQUIRE(values[0] == 1.f);
    REQUIRE(values[1] == 1.f);
    REQUIRE(values[2] == 1.f);
    REQUIRE(values[3] == 0.f);  // ← float3-declared: 4th slot forced to 0
}

TEST_CASE("Float3 zero-out also fires when source material overrides the value") {
    // Even when ExportMaterial::params supplies a non-default value with
    // alpha=1 (as Max's TYPE_FRGBA does), the writer should strip the alpha
    // for float3-declared params.
    ExportScene s = ExportScene::with_root_bone();
    ExportBone b; b.name = "Cube"; b.parent_index = 0; s.bones.push_back(b);
    ExportMesh m;  m.name = "Cube"; m.bone_index = 1;
    ExportSubmesh sub;
    sub.material.shader_name = "MeshBumpColorize.fx";  // Specular is float3
    MaterialParam p;
    p.name = "Specular";
    p.kind = MaterialParam::Kind::Float4;
    p.value4 = { 0.3f, 0.3f, 0.3f, 1.0f };  // alpha=1 as Max would supply
    sub.material.params.push_back(p);
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
    // MeshBumpColorize order: shader_name, Emissive, Diffuse, Specular, ...
    const ChunkNode& specular = mat.children[3];
    REQUIRE(specular.id == 0x10106);
    float v[4];
    std::memcpy(v, specular.payload.data() + specular.payload.size() - 16, 16);
    REQUIRE(v[0] == 0.3f);
    REQUIRE(v[1] == 0.3f);
    REQUIRE(v[2] == 0.3f);
    REQUIRE(v[3] == 0.f);  // ← override alpha=1 still zeroed out
}

TEST_CASE("Genuine float4 params keep the 4th slot (Colorization, UVOffset, ...)") {
    // MeshBumpColorize's Colorization is declared `float4` in
    // BumpColorize.fxh -- vanilla content writes the full 4 components,
    // including alpha. Our default = (0, 1, 0, 1); 4th slot must NOT be zeroed.
    ExportScene s = ExportScene::with_root_bone();
    ExportBone b; b.name = "M"; b.parent_index = 0; s.bones.push_back(b);
    ExportMesh m; m.name = "M"; m.bone_index = 1;
    ExportSubmesh sub;
    sub.material.shader_name = "MeshBumpColorize.fx";
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
    // MeshBumpColorize order: shader_name, Emissive, Diffuse, Specular,
    // Shininess, Colorization, UVOffset, [BaseTexture, NormalTexture]
    const ChunkNode& colorization = mat.children[5];
    REQUIRE(colorization.id == 0x10106);
    float v[4];
    std::memcpy(v, colorization.payload.data() + colorization.payload.size() - 16, 16);
    REQUIRE(v[0] == 0.f);
    REQUIRE(v[1] == 1.f);
    REQUIRE(v[2] == 0.f);
    REQUIRE(v[3] == 1.f);  // ← genuine float4: 4th slot preserved
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

// ---- Phase 7a: lights -----------------------------------------------------

namespace {
ExportScene scene_with_one_omni_light()
{
    ExportScene s = ExportScene::with_root_bone();
    // A synthetic per-light bone, mirroring the per-mesh-bone pattern.
    ExportBone lb;
    lb.name         = "Omni01";
    lb.parent_index = 0;
    s.bones.push_back(lb);

    ExportLight light;
    light.name        = "Omni01";
    light.type        = ExportLight::Type::Omni;
    light.color       = {0.8f, 0.6f, 0.2f};
    light.intensity   = 1.5f;
    light.atten_end   = 100.f;
    light.atten_start = 30.f;
    light.hotspot     = 0.f;   // unused for omni
    light.falloff     = 0.f;
    light.bone_index  = 1;
    s.lights.push_back(std::move(light));
    return s;
}
}  // namespace

TEST_CASE("build_alo with a light slots 0x1300 between 0x400 meshes and 0x600 connections") {
    auto s = scene_with_one_omni_light();
    auto tree = build_alo(s);

    // Layout: [0x200 skeleton] [0x1300 light] [0x600 connections].
    // No meshes in this scene -- the light still slots between meshes
    // (zero of them) and connections.
    REQUIRE(tree.size() == 3);
    REQUIRE(tree[0].id == 0x200);
    REQUIRE(tree[1].id == 0x1300);
    REQUIRE(tree[1].is_container);
    REQUIRE(tree[2].id == 0x600);
}

TEST_CASE("0x1300 container has 0x1301 (name) + 0x1302 (36-byte data) children") {
    auto tree = build_alo(scene_with_one_omni_light());
    const ChunkNode& light = tree[1];
    REQUIRE(light.children.size() == 2);

    const ChunkNode& name_chunk = light.children[0];
    REQUIRE(name_chunk.id == 0x1301);
    REQUIRE_FALSE(name_chunk.is_container);
    // Name "Omni01" + trailing NUL = 7 bytes.
    REQUIRE(name_chunk.payload.size() == 7);

    const ChunkNode& data_chunk = light.children[1];
    REQUIRE(data_chunk.id == 0x1302);
    REQUIRE_FALSE(data_chunk.is_container);
    REQUIRE(data_chunk.payload.size() == 36);
}

TEST_CASE("0x1302 payload byte layout matches Mike Lankamp's reader (type, RGB, intensity, attenEnd, attenStart, hotspot, falloff)") {
    auto tree = build_alo(scene_with_one_omni_light());
    const auto& p = tree[1].children[1].payload;
    REQUIRE(p.size() == 36);

    std::uint32_t type;
    std::memcpy(&type, p.data() + 0, 4);
    REQUIRE(type == 0);  // Omni

    float r, g, b;
    std::memcpy(&r, p.data() + 4,  4);
    std::memcpy(&g, p.data() + 8,  4);
    std::memcpy(&b, p.data() + 12, 4);
    REQUIRE(r == 0.8f);
    REQUIRE(g == 0.6f);
    REQUIRE(b == 0.2f);

    float intensity, atten_end, atten_start, hotspot, falloff;
    std::memcpy(&intensity,   p.data() + 16, 4);
    std::memcpy(&atten_end,   p.data() + 20, 4);
    std::memcpy(&atten_start, p.data() + 24, 4);
    std::memcpy(&hotspot,     p.data() + 28, 4);
    std::memcpy(&falloff,     p.data() + 32, 4);
    REQUIRE(intensity   == 1.5f);
    REQUIRE(atten_end   == 100.f);
    REQUIRE(atten_start == 30.f);
    REQUIRE(hotspot     == 0.f);
    REQUIRE(falloff     == 0.f);
}

TEST_CASE("Spotlight type is 2; directional is 1") {
    ExportScene s = ExportScene::with_root_bone();
    ExportBone bone; bone.name = "Spot"; bone.parent_index = 0; s.bones.push_back(bone);

    ExportLight spot;
    spot.name = "Spot"; spot.type = ExportLight::Type::Spotlight;
    spot.hotspot = 0.5f; spot.falloff = 0.8f; spot.bone_index = 1;
    s.lights.push_back(spot);

    auto tree = build_alo(s);
    const auto& payload = tree[1].children[1].payload;
    std::uint32_t type;
    std::memcpy(&type, payload.data(), 4);
    REQUIRE(type == 2);

    // Directional check via a second scene.
    ExportScene s2 = ExportScene::with_root_bone();
    ExportBone db; db.name = "Dir"; db.parent_index = 0; s2.bones.push_back(db);
    ExportLight dir;
    dir.name = "Dir"; dir.type = ExportLight::Type::Directional; dir.bone_index = 1;
    s2.lights.push_back(dir);
    auto tree2 = build_alo(s2);
    std::memcpy(&type, tree2[1].children[1].payload.data(), 4);
    REQUIRE(type == 1);
}

// ---- Phase 7a: proxies ----------------------------------------------------

TEST_CASE("build_proxy emits 0x603 leaf with required mini-chunks 5 (name) + 6 (bone), no others when defaults") {
    ExportScene s = ExportScene::with_root_bone();
    ExportBone pb; pb.name = "p_smoke"; pb.parent_index = 0; s.bones.push_back(pb);
    ExportProxy proxy;
    proxy.name = "p_smoke";
    proxy.bone_index = 1;
    s.proxies.push_back(proxy);

    auto tree = build_alo(s);
    // [0x200] [0x600 connections (with one 0x603 inside)]
    REQUIRE(tree.size() == 2);
    const ChunkNode& connections = tree[1];
    REQUIRE(connections.id == 0x600);
    // children: [0x601 counts] [0x603 proxy]. No meshes / lights so
    // there are no 0x602 entries.
    REQUIRE(connections.children.size() == 2);
    REQUIRE(connections.children[0].id == 0x601);
    REQUIRE(connections.children[1].id == 0x603);

    // 0x603 payload layout: mini 5 (name) + mini 6 (bone). Nothing else
    // because is_hidden and alt_decrease_stay_hidden default-false.
    const auto& p = connections.children[1].payload;
    //   [t=5][sz=8]"p_smoke\0"  [t=6][sz=4][bone_u32]
    REQUIRE(p.size() == 2 + 8 + 2 + 4);
    REQUIRE(p[0] == 5);
    REQUIRE(p[1] == 8);
    REQUIRE(std::string(reinterpret_cast<const char*>(p.data() + 2)) == "p_smoke");
    REQUIRE(p[10] == 6);
    REQUIRE(p[11] == 4);
    std::uint32_t bone_idx;
    std::memcpy(&bone_idx, p.data() + 12, 4);
    REQUIRE(bone_idx == 1);
}

TEST_CASE("build_proxy includes mini-chunks 7 and 8 only when their flags are set") {
    ExportScene s = ExportScene::with_root_bone();
    ExportBone pb; pb.name = "p_fx"; pb.parent_index = 0; s.bones.push_back(pb);
    ExportProxy proxy;
    proxy.name = "p_fx";
    proxy.bone_index = 1;
    proxy.is_hidden = true;
    proxy.alt_decrease_stay_hidden = true;
    s.proxies.push_back(proxy);

    auto tree = build_alo(s);
    const auto& p = tree[1].children[1].payload;
    // [5][5]"p_fx\0" [6][4][bone] [7][4][1] [8][4][1]
    REQUIRE(p.size() == (2 + 5) + (2 + 4) + (2 + 4) + (2 + 4));

    // Scan for the optional mini-chunks. Easier than computing offsets
    // by hand for every test variant.
    bool saw_hidden = false, saw_alt = false;
    std::size_t off = 0;
    while (off + 2 <= p.size()) {
        std::uint8_t t = p[off];
        std::uint8_t sz = p[off + 1];
        if (t == 7) {
            saw_hidden = true;
            std::uint32_t v;
            std::memcpy(&v, p.data() + off + 2, 4);
            REQUIRE(v == 1u);
        }
        if (t == 8) {
            saw_alt = true;
            std::uint32_t v;
            std::memcpy(&v, p.data() + off + 2, 4);
            REQUIRE(v == 1u);
        }
        off += 2 + sz;
    }
    REQUIRE(saw_hidden);
    REQUIRE(saw_alt);
}

// ---- Phase 7a: connections cover meshes + lights + proxies ----------------

TEST_CASE("0x601 counts reflect (mesh+light) connection count and proxy count") {
    ExportScene s = minimal_cube_scene();   // 1 mesh + its bone
    // Add a light at the end.
    ExportBone lb; lb.name = "Omni"; lb.parent_index = 0; s.bones.push_back(lb);
    ExportLight light;
    light.name = "Omni"; light.bone_index = 2;
    s.lights.push_back(light);
    // And two proxies.
    ExportBone pb1; pb1.name = "p_a"; pb1.parent_index = 0; s.bones.push_back(pb1);
    ExportBone pb2; pb2.name = "p_b"; pb2.parent_index = 0; s.bones.push_back(pb2);
    ExportProxy pa; pa.name = "p_a"; pa.bone_index = 3; s.proxies.push_back(pa);
    ExportProxy pb; pb.name = "p_b"; pb.bone_index = 4; s.proxies.push_back(pb);

    auto tree = build_alo(s);
    // [0x200] [0x400 mesh] [0x1300 light] [0x600 connections]
    REQUIRE(tree.size() == 4);
    const ChunkNode& connections = tree[3];

    // 0x601 is the first child of 0x600. Its mini-chunks are
    // [t=1][sz=4][nConn=2] [t=4][sz=4][nProxy=2].
    const auto& counts = connections.children[0];
    REQUIRE(counts.id == 0x601);
    REQUIRE(counts.payload.size() == 12);
    std::uint32_t n_conn, n_prox;
    std::memcpy(&n_conn, counts.payload.data() + 2, 4);
    std::memcpy(&n_prox, counts.payload.data() + 8, 4);
    REQUIRE(n_conn == 2);  // 1 mesh + 1 light
    REQUIRE(n_prox == 2);
}

TEST_CASE("0x602 object indices are monotonically increasing 0,1,2,... (mesh-then-light order)") {
    // Two meshes + one light -> object indices 0, 1, 2.
    ExportScene s = ExportScene::with_root_bone();
    for (int i = 0; i < 2; ++i) {
        ExportBone b; b.name = "m" + std::to_string(i); b.parent_index = 0;
        s.bones.push_back(b);
        ExportMesh m; m.name = "m" + std::to_string(i);
        m.bone_index = static_cast<std::uint32_t>(s.bones.size() - 1);
        ExportSubmesh sub; sub.material.shader_name = "MeshAlpha.fx";
        ExportVertex v;
        v.bone_indices = {m.bone_index, 0u, 0u, 0u};
        sub.vertices.push_back(v); sub.indices.push_back(0);
        m.submeshes.push_back(std::move(sub));
        s.meshes.push_back(std::move(m));
    }
    ExportBone lb; lb.name = "L"; lb.parent_index = 0; s.bones.push_back(lb);
    ExportLight light; light.name = "L";
    light.bone_index = static_cast<std::uint32_t>(s.bones.size() - 1);
    s.lights.push_back(light);

    auto tree = build_alo(s);
    // tree[-1] is the connections container.
    const ChunkNode& connections = tree.back();
    REQUIRE(connections.id == 0x600);
    // children: [0x601 counts] then 3 × 0x602.
    REQUIRE(connections.children.size() == 4);

    for (std::uint32_t i = 0; i < 3; ++i) {
        const ChunkNode& conn = connections.children[1 + i];
        REQUIRE(conn.id == 0x602);
        // Payload: [t=2][sz=4][object_idx] [t=3][sz=4][bone_idx]
        REQUIRE(conn.payload.size() == 12);
        REQUIRE(conn.payload[0] == 2);
        std::uint32_t obj_idx;
        std::memcpy(&obj_idx, conn.payload.data() + 2, 4);
        REQUIRE(obj_idx == i);  // monotonic
    }
}

TEST_CASE("Mixed scene (mesh + light + proxy) round-trips byte-identical via write/read") {
    ExportScene s = minimal_cube_scene();
    ExportBone lb; lb.name = "Omni"; lb.parent_index = 0; s.bones.push_back(lb);
    ExportLight light;
    light.name = "Omni"; light.intensity = 2.f; light.bone_index = 2;
    s.lights.push_back(light);
    ExportBone pb; pb.name = "p_fx"; pb.parent_index = 0; s.bones.push_back(pb);
    ExportProxy proxy;
    proxy.name = "p_fx"; proxy.bone_index = 3; proxy.is_hidden = true;
    s.proxies.push_back(proxy);

    auto tree = build_alo(s);
    auto bytes = write_chunk_tree(tree);
    auto reparsed = read_chunk_tree(bytes.data(), bytes.size());
    auto bytes2 = write_chunk_tree(reparsed);
    REQUIRE(bytes2 == bytes);
}


namespace {

// Helper: scan the chunk tree depth-first for every 0x10002 leaf and
// return their cstring payloads. Used by the Phase 10 wiring tests
// to assert that ExportSubmesh::vertex_format_name flows through the
// writer.
std::vector<std::string> collect_vertex_format_strings(const std::vector<ChunkNode>& roots) {
    std::vector<std::string> out;
    std::function<void(const ChunkNode&)> walk = [&](const ChunkNode& n) {
        if (n.id == 0x10002u && !n.is_container) {
            const auto& p = n.payload;
            std::size_t nul = 0;
            while (nul < p.size() && p[nul] != 0u) ++nul;
            out.emplace_back(reinterpret_cast<const char*>(p.data()), nul);
        }
        if (n.is_container) {
            for (const auto& c : n.children) walk(c);
        }
    };
    for (const auto& r : roots) walk(r);
    return out;
}

}  // namespace

TEST_CASE("ExportSubmesh::vertex_format_name flows through to 0x10002 (Phase 10)") {
    SECTION("empty field defaults to alD3dVertNU2 (Phase 4 back-compat)") {
        ExportScene s = minimal_cube_scene();
        REQUIRE(s.meshes[0].submeshes[0].vertex_format_name.empty());
        auto tree = build_alo(s);
        auto strings = collect_vertex_format_strings(tree);
        REQUIRE(strings.size() == 1);
        REQUIRE(strings[0] == "alD3dVertNU2");
    }
    SECTION("populated field is emitted verbatim") {
        ExportScene s = minimal_cube_scene();
        s.meshes[0].submeshes[0].vertex_format_name = "alD3dVertRSkinNU2";
        auto tree = build_alo(s);
        auto strings = collect_vertex_format_strings(tree);
        REQUIRE(strings.size() == 1);
        REQUIRE(strings[0] == "alD3dVertRSkinNU2");
    }
    SECTION("multiple submeshes get independent strings") {
        ExportScene s = minimal_cube_scene();
        s.meshes[0].submeshes[0].vertex_format_name = "alD3dVertRSkinNU2U3U3";
        ExportSubmesh sub2 = s.meshes[0].submeshes[0];
        sub2.vertex_format_name = "alD3dVertGrass";
        s.meshes[0].submeshes.push_back(sub2);
        auto tree = build_alo(s);
        auto strings = collect_vertex_format_strings(tree);
        REQUIRE(strings.size() == 2);
        REQUIRE(strings[0] == "alD3dVertRSkinNU2U3U3");
        REQUIRE(strings[1] == "alD3dVertGrass");
    }
}


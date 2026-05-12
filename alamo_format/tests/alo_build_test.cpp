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

    ExportMesh mesh;
    mesh.name = "Cube";
    mesh.bbox_min = { -1.f, -1.f, -1.f };
    mesh.bbox_max = {  1.f,  1.f,  1.f };

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
    REQUIRE(bone_count == 1);
    // Remaining 124 bytes must be zero.
    for (std::size_t i = 4; i < 128; ++i) {
        REQUIRE(info.payload[i] == 0);
    }
}

TEST_CASE("Bone container has 0x203 name + 0x206 data of 60 bytes") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& bone = tree[0].children[1];
    REQUIRE(bone.id == 0x202);
    REQUIRE(bone.children.size() == 2);

    REQUIRE(bone.children[0].id == 0x203);
    REQUIRE(bone.children[0].payload == std::vector<std::uint8_t>{'R', 'o', 'o', 't', 0});

    REQUIRE(bone.children[1].id == 0x206);
    REQUIRE(bone.children[1].payload.size() == 60);
}

TEST_CASE("Mesh info is exactly 128 bytes; first u32 is material count") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& mesh = tree[1];
    // children: 0x401 name, 0x402 info, 0x10100 submesh
    REQUIRE(mesh.children.size() == 3);
    const ChunkNode& info = mesh.children[1];
    REQUIRE(info.id == 0x402);
    REQUIRE(info.payload.size() == 128);
    std::uint32_t mat_count = 0;
    std::memcpy(&mat_count, info.payload.data(), 4);
    REQUIRE(mat_count == 1);
}

TEST_CASE("Submesh emits shader name, BaseTexture, and geometry container") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& submesh = tree[1].children[2];
    REQUIRE(submesh.id == 0x10100);
    REQUIRE(submesh.children.size() == 3);
    REQUIRE(submesh.children[0].id == 0x10101);
    REQUIRE(submesh.children[1].id == 0x10105);  // TEXTURE param
    REQUIRE(submesh.children[2].id == 0x10000);
}

TEST_CASE("Vertex chunk uses 0x10007 with 144 bytes per vertex") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& submesh = tree[1].children[2];
    const ChunkNode& geom    = submesh.children[2];
    REQUIRE(geom.id == 0x10000);
    // children: 0x10001 sizes, 0x10002 format name, 0x10007 verts, 0x10004 faces
    REQUIRE(geom.children.size() == 4);
    REQUIRE(geom.children[2].id == 0x10007);
    REQUIRE(geom.children[2].payload.size() == 36u * 144u);
}

TEST_CASE("Face chunk encodes 3 uint16 indices per triangle") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& geom = tree[1].children[2].children[2];
    const ChunkNode& faces = geom.children[3];
    REQUIRE(faces.id == 0x10004);
    REQUIRE(faces.payload.size() == 36u * 2u);  // 36 indices * 2 bytes each
}

TEST_CASE("Connections section emits one 0x602 per mesh, all bound to bone 0") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& conns = tree[2];
    REQUIRE(conns.id == 0x600);
    REQUIRE(conns.children[0].id == 0x601);
    REQUIRE(conns.children.size() == 2);  // 0x601 counts + 1 x 0x602

    const ChunkNode& obj = conns.children[1];
    REQUIRE(obj.id == 0x602);
    // Mini-chunk 2 (object idx) at offset 0..6, then mini-chunk 3 (bone idx)
    REQUIRE(obj.payload[0] == 2);  // mini ID
    REQUIRE(obj.payload[1] == 4);  // payload size
    std::uint32_t obj_idx = 0;
    std::memcpy(&obj_idx, obj.payload.data() + 2, 4);
    REQUIRE(obj_idx == 0u);
    REQUIRE(obj.payload[6] == 3);  // mini ID
    REQUIRE(obj.payload[7] == 4);  // payload size
    std::uint32_t bone_idx = 0;
    std::memcpy(&bone_idx, obj.payload.data() + 8, 4);
    REQUIRE(bone_idx == 0u);
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

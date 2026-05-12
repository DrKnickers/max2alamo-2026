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

TEST_CASE("0x10100 contains shader name + BaseTexture but NOT geometry") {
    auto tree = build_alo(minimal_cube_scene());
    const ChunkNode& mat = tree[1].children[2];
    REQUIRE(mat.id == 0x10100);
    REQUIRE(mat.children.size() == 2);
    REQUIRE(mat.children[0].id == 0x10101);
    REQUIRE(mat.children[1].id == 0x10105);  // TEXTURE param
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

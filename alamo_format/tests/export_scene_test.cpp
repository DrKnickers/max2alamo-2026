#include <catch2/catch_test_macros.hpp>

#include "alamo_format/export_scene.h"

using namespace alamo_format;

TEST_CASE("ExportScene::with_root_bone seeds exactly one Root bone") {
    auto s = ExportScene::with_root_bone();
    REQUIRE(s.bones.size() == 1);
    REQUIRE(s.bones[0].name == "Root");
    REQUIRE(s.bones[0].parent_index == ExportBone::kRootParent);
    REQUIRE(s.bones[0].visible);
    REQUIRE(s.bones[0].billboard_mode == 0);
    REQUIRE(s.meshes.empty());
}

TEST_CASE("Default ExportBone matrix is identity in column-major 4x3 layout") {
    ExportBone b;
    // Columns 0..2 form the rotation; column 3 is the translation.
    REQUIRE(b.matrix[0]  == 1.f); REQUIRE(b.matrix[1]  == 0.f); REQUIRE(b.matrix[2]  == 0.f);
    REQUIRE(b.matrix[3]  == 0.f); REQUIRE(b.matrix[4]  == 1.f); REQUIRE(b.matrix[5]  == 0.f);
    REQUIRE(b.matrix[6]  == 0.f); REQUIRE(b.matrix[7]  == 0.f); REQUIRE(b.matrix[8]  == 1.f);
    REQUIRE(b.matrix[9]  == 0.f); REQUIRE(b.matrix[10] == 0.f); REQUIRE(b.matrix[11] == 0.f);
}

TEST_CASE("ExportMesh / ExportSubmesh / ExportVertex compose by value") {
    ExportScene s = ExportScene::with_root_bone();

    ExportMesh mesh;
    mesh.name = "TestCube";
    mesh.bbox_min = { -1.f, -1.f, -1.f };
    mesh.bbox_max = {  1.f,  1.f,  1.f };

    ExportSubmesh sub;
    sub.material.shader_name  = "MeshAlpha.fx";
    sub.material.base_texture = "tex_test.tga";
    sub.vertices.push_back({{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.5f, 0.5f}});
    sub.indices.push_back(0);

    mesh.submeshes.push_back(std::move(sub));
    s.meshes.push_back(std::move(mesh));

    REQUIRE(s.meshes.size() == 1);
    REQUIRE(s.meshes[0].name == "TestCube");
    REQUIRE(s.meshes[0].submeshes.size() == 1);
    REQUIRE(s.meshes[0].submeshes[0].material.shader_name  == "MeshAlpha.fx");
    REQUIRE(s.meshes[0].submeshes[0].material.base_texture == "tex_test.tga");
    REQUIRE(s.meshes[0].submeshes[0].vertices.size() == 1);
    REQUIRE(s.meshes[0].submeshes[0].vertices[0].normal[2] == 1.f);
    REQUIRE(s.meshes[0].submeshes[0].vertices[0].uv[0] == 0.5f);
}

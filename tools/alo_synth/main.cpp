// alo_synth: synthesise an .alo from a hard-coded ExportScene to verify
// the Phase 6c material-parameter pipeline end-to-end without needing
// 3ds Max. Builds an exporter-side scene with MeshBumpColorize + a known
// set of parameter overrides, runs build_alo + write_alo, and prints
// the resulting .alo's path. Run alo_dump on the output to confirm the
// material chunks match the vanilla layout.

#include "alamo_format/alo_build.h"
#include "alamo_format/chunk_tree.h"

#include <fstream>
#include <iostream>
#include <string>

using namespace alamo_format;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: alo_synth <out.alo>\n";
        return 2;
    }

    ExportScene s = ExportScene::with_root_bone();

    // Per-mesh attachment bone, matching what scene_walker emits.
    ExportBone b;
    b.name         = "Box";
    b.parent_index = 0;
    b.matrix = { 1,0,0,10, 0,1,0,0, 0,0,1,5 };  // translate (10, 0, 5)
    s.bones.push_back(b);

    ExportMesh m;
    m.name       = "Box";
    m.bone_index = 1;
    m.bbox_min   = { -1, -1, -1 };
    m.bbox_max   = {  1,  1,  1 };

    ExportSubmesh sub;
    sub.material.shader_name  = "MeshBumpColorize.fx";
    sub.material.base_texture = "ignored.dds";  // overridden below

    // Simulate what Phase 6c's walker would have extracted from a Max
    // DirectX Shader material whose parameters the user has tweaked.
    auto add_f4 = [&](std::string n, float a, float b, float c, float d) {
        MaterialParam p; p.name = std::move(n);
        p.kind = MaterialParam::Kind::Float4;
        p.value4 = {a, b, c, d};
        sub.material.params.push_back(p);
    };
    auto add_f = [&](std::string n, float v) {
        MaterialParam p; p.name = std::move(n);
        p.kind = MaterialParam::Kind::Float;
        p.value4 = {v, 0, 0, 0};
        sub.material.params.push_back(p);
    };
    auto add_tex = [&](std::string n, std::string fn) {
        MaterialParam p; p.name = std::move(n);
        p.kind = MaterialParam::Kind::Texture;
        p.texture = std::move(fn);
        sub.material.params.push_back(p);
    };

    add_f4 ("Emissive",     0.0f, 0.0f, 0.0f, 0.0f);
    add_f4 ("Diffuse",      0.8f, 0.8f, 0.8f, 0.0f);
    add_f4 ("Specular",     0.3f, 0.3f, 0.3f, 0.0f);  // <-- intentionally tuned down
    add_f  ("Shininess",    8.0f);                    // <-- intentionally tuned down
    add_f4 ("Colorization", 0.0f, 1.0f, 0.0f, 1.0f);
    add_f4 ("UVOffset",     0.0f, 0.0f, 0.0f, 0.0f);
    add_tex("BaseTexture",  "Gallofree_HTT_26-cm.dds");
    add_tex("NormalTexture","Gallofree_HTT_26-nm.dds");

    // Minimal geometry: one tri so we hit the rev-2 144B vertex path.
    for (int i = 0; i < 3; ++i) {
        ExportVertex v;
        v.position = { float(i), 0.f, 0.f };
        v.normal   = { 0.f, 0.f, 1.f };
        v.uv       = { 0.5f, 0.5f };
        v.tangent  = { 1.f, 0.f, 0.f };
        v.binormal = { 0.f, 1.f, 0.f };
        sub.vertices.push_back(v);
        sub.indices.push_back(static_cast<std::uint32_t>(i));
    }
    m.submeshes.push_back(std::move(sub));
    s.meshes.push_back(std::move(m));

    auto tree  = build_alo(s);
    auto bytes = write_chunk_tree(tree);

    std::ofstream out(argv[1], std::ios::binary);
    if (!out) {
        std::cerr << "could not open " << argv[1] << " for writing\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    std::cout << "wrote " << bytes.size() << " bytes to " << argv[1] << "\n";
    return 0;
}

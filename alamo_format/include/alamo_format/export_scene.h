#pragma once

// ExportScene: the airlock between a host application (3ds Max in v1; could
// be Maya / Blender / a CLI tool later) and the alamo_format writer.
//
// All fields are POD-style: plain types, std::vector / std::string only.
// No 3ds Max types appear here, by design -- the format library must stay
// SDK-free so it can build in CI and be unit-tested without launching Max.
//
// Phase 4 fills these structs from a Max scene; Phase 4b adds the
// ExportScene -> ChunkNode tree converter that hands off to write_chunk_tree.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace alamo_format {

// Per-vertex data for the alD3dVertNU2 layout (Phase 4 default).
// Other vertex formats (RSkin*, B4I4*, with tangent / color / etc.) get
// their own structs as they come online in later phases. UV is already
// V-flipped at scene-walk time so downstream code never has to think
// about it.
struct ExportVertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 2> uv;
};

// Material assignment for a submesh. Phase 4 ships `shader_name` and
// `base_texture` only; typed shader parameters (Emissive, Diffuse,
// Specular, etc.) arrive in Phase 6 alongside the data-driven
// shader_table.h.
struct ExportMaterial {
    std::string shader_name;    // e.g. "MeshAlpha.fx"
    std::string base_texture;   // e.g. "tex_box.tga"; empty for no texture
};

// One material slice of a mesh. A multi-material Max object becomes one
// ExportSubmesh per material in the same ExportMesh; Phase 4 only emits
// the first material (single-material assumption documented in the
// development log).
struct ExportSubmesh {
    ExportMaterial            material;
    std::vector<ExportVertex> vertices;
    std::vector<std::uint32_t> indices;   // 3 per face, expanded
};

struct ExportMesh {
    std::string                 name;
    std::vector<ExportSubmesh>  submeshes;
    bool                        is_hidden    = false;
    bool                        is_collision = false;
    std::array<float, 3>        bbox_min{0.f, 0.f, 0.f};
    std::array<float, 3>        bbox_max{0.f, 0.f, 0.f};

    // Index into ExportScene::bones of the bone this mesh attaches to.
    // Vanilla static-prop layout: every mesh has its own non-Root bone
    // (parent = 0 = Root) named after the mesh; the 0x602 connection
    // binds the mesh to it; per-vertex boneIdx points at it. Mike
    // Lankamp's importer assumes this layout (it deletes the synthetic
    // Root bone on import and then expects vertex / connection bone
    // references to land on real bones); a 1-bone scene crashes him.
    std::uint32_t               bone_index   = 0;
};

// Bone in the skeleton hierarchy. Phase 4 emits exactly one synthetic
// "Root" bone with parent_index = kRootParent so the skeleton chunk is
// well-formed; real bone import lands in Phase 5.
struct ExportBone {
    static constexpr std::uint32_t kRootParent = 0xFFFFFFFFu;

    std::string                 name;
    std::uint32_t               parent_index   = kRootParent;
    bool                        visible        = true;
    std::uint32_t               billboard_mode = 0;

    // Transform stored as a 4x3 matrix in COLUMN-MAJOR order: 3 columns
    // of 4 elements each. The conceptual matrix is:
    //
    //     | r1x r1y r1z |    row 1 = X axis
    //     | r2x r2y r2z |    row 2 = Y axis
    //     | r3x r3y r3z |    row 3 = Z axis
    //     | tx  ty  tz  |    row 4 = translation
    //
    // On-disk layout (matches Mike Lankamp's reader in alamo2max.ms:374-378,
    // which reads c[1..12] sequentially and builds a Max Matrix3 via
    //   Matrix3 [c[1],c[5],c[9]] [c[2],c[6],c[10]] [c[3],c[7],c[11]] [c[4],c[8],c[12]]
    // which is column-major-by-element):
    //
    //     matrix[0..4]   = column 0 = (r1x, r2x, r3x, tx)
    //     matrix[4..8]   = column 1 = (r1y, r2y, r3y, ty)
    //     matrix[8..12]  = column 2 = (r1z, r2z, r3z, tz)
    //
    // Identity transform in this layout: column 0 = (1,0,0,0), column 1 =
    // (0,1,0,0), column 2 = (0,0,1,0). Phase 5 will bake real Max
    // transforms here.
    std::array<float, 12>       matrix{1.f, 0.f, 0.f, 0.f,
                                       0.f, 1.f, 0.f, 0.f,
                                       0.f, 0.f, 1.f, 0.f};
};

// Full snapshot of an exportable scene. The writer in Phase 4b consumes
// this; the Max-side walker in Phase 4a produces it. Future host walkers
// (Maya / Blender / etc.) would also produce ExportScene, keeping the
// writer host-agnostic.
struct ExportScene {
    std::vector<ExportBone> bones;
    std::vector<ExportMesh> meshes;
    // Future: lights, proxies, connections, animation clips...

    // Returns the placeholder root-bone scene every Phase 4 export starts
    // from: one bone named "Root", identity transform, no meshes. Callers
    // append meshes after constructing.
    static ExportScene with_root_bone()
    {
        ExportScene s;
        ExportBone root;
        root.name = "Root";
        root.parent_index = ExportBone::kRootParent;
        root.visible = true;
        s.bones.push_back(root);
        return s;
    }
};

}  // namespace alamo_format

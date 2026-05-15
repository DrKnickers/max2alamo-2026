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
    // Tangent-space basis (Phase 6b). Required by PG's bump / normal-mapped
    // shaders (MeshBumpColorize, RSkinBumpColorize, etc.) which sample these
    // in the vertex shader to build the world-to-tangent matrix. Convention
    // matches MikkTSpace (Max 2026 default): unit length, perpendicular to
    // `normal`, with handedness baked into `binormal`'s sign such that
    // binormal = sign * cross(normal, tangent). All-zero values are a
    // historical default from Phase 4 and produce broken bump lighting at
    // runtime; the scene walker now populates them from the source mesh.
    std::array<float, 3> tangent{0.f, 0.f, 0.f};
    std::array<float, 3> binormal{0.f, 0.f, 0.f};
    // Per-vertex skin binding. For static / rigid-attachment meshes the
    // walker writes the same per-mesh bone into slot 0 of every vertex
    // with weight 1.0 (Phase 4c convention). For skinned meshes (those
    // with a Skin modifier) the top 4 weighted influences from the
    // IGameSkin modifier populate slots 0..3 (Phase 5c) -- weights are
    // renormalized to sum to 1.0; vertices with <4 influences leave
    // trailing slots at (index=0, weight=0). Default is "rigidly bound
    // to Root", which is a safe sentinel for any caller that forgets to
    // populate explicitly.
    std::array<std::uint32_t, 4> bone_indices{0u, 0u, 0u, 0u};
    std::array<float, 4>         weights     {1.f, 0.f, 0.f, 0.f};
};

// One named parameter on a material. The kind drives both the on-disk
// chunk ID and which `value*` slot carries the data. Per vanilla
// convention every scalar / vector param is always emitted (even at its
// default value); texture params are emitted only when the filename is
// non-empty.
//
// Vanilla content uses 4-element vectors even for parameters that PG
// declared as `float3` in the .fxh (Emissive, Diffuse, Specular, etc.) --
// the 4th element is just zero. We follow that convention: kind=Float4
// covers both float3 and float4 declarations.
struct MaterialParam {
    enum class Kind : std::uint8_t { Float, Float4, Texture };
    std::string             name;
    Kind                    kind = Kind::Float4;
    std::array<float, 4>    value4{0.f, 0.f, 0.f, 0.f};   // Float / Float4
    std::string             texture;                       // Texture only (basename)
};

// Material assignment for a submesh. Phase 4 shipped `shader_name` and
// `base_texture` only. Phase 6c populates `params` with the typed
// per-material parameter values (Emissive, Diffuse, Specular, ...) that
// the runtime shader reads instead of falling back to compile-time
// defaults. The order in `params` matches what vanilla content writes
// for the same shader (driven by alamo_format::shader_table::params_for).
struct ExportMaterial {
    std::string                shader_name;    // e.g. "MeshAlpha.fx"
    std::string                base_texture;   // back-compat shortcut for the
                                               // BaseTexture entry; mirrored
                                               // into `params` at build time.
    std::vector<MaterialParam> params;
};

// One material slice of a mesh. A multi-material Max object becomes one
// ExportSubmesh per material in the same ExportMesh; Phase 4 only emits
// the first material (single-material assumption documented in the
// development log).
struct ExportSubmesh {
    ExportMaterial            material;
    // 0x10002 vertex-format-name string. The engine's renderer reads
    // this via case-insensitive lookup into AloViewer's 15-entry
    // VertexFormatNames table to bind the GPU vertex declaration;
    // misreporting it breaks rendering at the shader-input level
    // (skinned meshes collapse, etc.). Phase 10 populated by the
    // walker via vertex_format_selector::default_vertex_format_for_shader
    // for stock PG shaders. Empty = fall back to the basic
    // `alD3dVertNU2` (Phase 4 default, preserves back-compat for
    // callers / tests that don't set it).
    std::string               vertex_format_name;
    // 0x10006 skin-bone-remap table. Per AloViewer's loader
    // (`src/Assets/Models.cpp:155`), this is a flat list of global
    // skeleton bone indices; the renderer dereferences each per-vertex
    // `bone_indices[i]` value through this list to get the actual
    // bone matrix (`bones[skin_bone_remap[bone_indices[i]]]`). So when
    // this list is populated, `ExportVertex::bone_indices` MUST contain
    // LOCAL slot indices (0..N-1 where N = `skin_bone_remap.size()`),
    // not global skeleton indices. Phase 10.5 (#81): only populated
    // for skinned submeshes (vertex_format_name starts with
    // `alD3dVertRSkin` or `alD3dVertB4I4`); empty for static meshes.
    // Empty = writer omits the 0x10006 chunk.
    std::vector<std::uint32_t> skin_bone_remap;
    std::vector<ExportVertex> vertices;
    std::vector<std::uint32_t> indices;   // 3 per face, expanded
};

struct ExportMesh {
    std::string                 name;
    std::vector<ExportSubmesh>  submeshes;
    bool                        is_hidden    = false;
    bool                        is_collision = false;

    // Walker-side diagnostics surfaced into .export.log without aborting
    // the export. Phase 12: shadow-volume closed-manifold violations are
    // the first user. Format: one human-readable line per warning, no
    // trailing newline. Empty by default.
    std::vector<std::string>    warnings;
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

// Phase 7a: 0x1300 light. A light is a "first-class connection object"
// in the .alo (it gets a 0x602 connection entry alongside meshes,
// indexed in (meshes ++ lights) order). Spotlight orientation is
// implicit: vanilla content emits the FreeSpot's bone PLUS a sibling
// '<name>.Target' bone, and the engine reconstructs direction from
// the two positions -- there is no explicit axis vector in 0x1302.
struct ExportLight {
    // On-disk type values from Mike Lankamp's alamo2max.ms enum:
    //   0 = OMNI, 1 = DIRECTIONAL, 2 = SPOTLIGHT.
    enum class Type : std::uint32_t {
        Omni        = 0,
        Directional = 1,
        Spotlight   = 2,
    };

    std::string             name;
    Type                    type        = Type::Omni;
    std::array<float, 3>    color       {1.f, 1.f, 1.f};  // linear RGB 0..1
    float                   intensity   = 1.f;
    float                   atten_end   = 0.f;            // farAttenuationEnd
    float                   atten_start = 0.f;            // farAttenuationStart
    float                   hotspot     = 0.f;            // radians (spotlight)
    float                   falloff     = 0.f;            // radians (spotlight)

    // Index into ExportScene::bones of the bone this light attaches
    // to (same convention as ExportMesh::bone_index). Walker creates
    // a synthetic per-light bone when the light isn't already
    // parented to one.
    std::uint32_t           bone_index  = 0;
};

// Phase 7a: 0x603 proxy. Vanilla content uses these as
// particle/effect attachment points (named 'p_*' by convention).
// They appear inside the 0x600 connections container after the
// 0x602 per-object connections. Mini-chunks 7/8 are emitted only
// when their respective bool is true -- vanilla content omits them
// when default, and Mike's reader treats both as optional.
struct ExportProxy {
    std::string             name;
    std::uint32_t           bone_index = 0;
    bool                    is_hidden = false;
    bool                    alt_decrease_stay_hidden = false;
};

// Full snapshot of an exportable scene. The writer in Phase 4b consumes
// this; the Max-side walker in Phase 4a produces it. Future host walkers
// (Maya / Blender / etc.) would also produce ExportScene, keeping the
// writer host-agnostic.
struct ExportScene {
    std::vector<ExportBone>  bones;
    std::vector<ExportMesh>  meshes;
    std::vector<ExportLight> lights;    // Phase 7a
    std::vector<ExportProxy> proxies;   // Phase 7a
    // Future: connections (currently derived), animation clips, dazzles (UaW non-goal).

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

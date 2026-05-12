// alo_dump: print the chunk tree of an .alo file.
//
// Usage:
//   alo_dump <file.alo> [--brief] [--max-depth N]
//
// The tool walks the chunked structure, printing each chunk's offset, ID,
// container/leaf status, payload size, and (for known IDs) a human label
// plus decoded key fields. Use --brief to suppress decoded fields and
// --max-depth to cap descent. Exit status 0 on success, 1 on parse error.

#include "alamo_format/chunk_io.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace alamo_format;

namespace {

struct Options {
    std::string path;
    bool brief = false;
    int max_depth = -1;  // -1 = unlimited
};

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--brief") {
            o.brief = true;
        } else if (a == "--max-depth" && i + 1 < argc) {
            o.max_depth = std::atoi(argv[++i]);
        } else if (a.rfind("-", 0) == 0) {
            std::fprintf(stderr, "alo_dump: unknown option '%s'\n", a.c_str());
            std::exit(EXIT_FAILURE);
        } else if (o.path.empty()) {
            o.path = a;
        } else {
            std::fprintf(stderr, "alo_dump: unexpected positional argument '%s'\n", a.c_str());
            std::exit(EXIT_FAILURE);
        }
    }
    if (o.path.empty()) {
        std::fprintf(stderr, "usage: alo_dump <file.alo> [--brief] [--max-depth N]\n");
        std::exit(EXIT_FAILURE);
    }
    return o;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    f.unsetf(std::ios::skipws);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

const char* chunk_label(std::uint32_t id) {
    switch (id) {
        // .alo
        case 0x200:    return "Skeleton";
        case 0x201:    return "Skeleton info";
        case 0x202:    return "Bone";
        case 0x203:    return "Bone name";
        case 0x205:    return "Bone data";
        case 0x206:    return "Bone data + billboard";
        case 0x400:    return "Mesh";
        case 0x401:    return "Mesh name";
        case 0x402:    return "Mesh info";
        case 0x600:    return "Connections";
        case 0x601:    return "Connection counts";
        case 0x602:    return "Connection: object -> bone";
        case 0x603:    return "Connection: proxy";
        case 0x604:    return "Connection: dazzle";
        case 0x10000:  return "Submesh data";
        case 0x10001:  return "Submesh sizes (vertex/face counts)";
        case 0x10002:  return "Submesh vertex format";
        case 0x10004:  return "Submesh face indices";
        case 0x10005:  return "Submesh vertices (rev 1, 128 B)";
        case 0x10006:  return "Submesh bone mapping";
        case 0x10007:  return "Submesh vertices (rev 2, 144 B)";
        case 0x10100:  return "Submesh material";
        case 0x10101:  return "Material shader name";
        case 0x10102:  return "Material param INT";
        case 0x10103:  return "Material param FLOAT";
        case 0x10104:  return "Material param FLOAT3";
        case 0x10105:  return "Material param TEXTURE";
        case 0x10106:  return "Material param FLOAT4";
        case 0x1200:   return "Collision tree";
        case 0x1300:   return "Light";
        case 0x1301:   return "Light name";
        case 0x1302:   return "Light data";
        // .ala
        case 0x1000:   return "Animation root";
        case 0x1001:   return "Animation info";
        case 0x1002:   return "Animation bone";
        case 0x1003:   return "Animation bone info";
        case 0x1004:   return "Anim translation track (EaW)";
        case 0x1005:   return "Anim scale track (EaW)";
        case 0x1006:   return "Anim rotation track (EaW)";
        case 0x1007:   return "Anim visibility track";
        case 0x1008:   return "Anim unknown 1008";
        case 0x1009:   return "Anim rotation pool (FoC)";
        case 0x100a:   return "Anim translation pool (FoC)";
        default:       return "(unknown)";
    }
}

void indent(int depth) {
    for (int i = 0; i < depth; ++i) std::printf("  ");
}

// Decode well-known leaf chunks for human readability. Called only when
// !brief and the chunk type matches.
void decode_known_leaf(const ChunkHeader& h, const std::uint8_t* data, int depth) {
    ChunkReader r(data + h.payload_offset, h.payload_size);
    indent(depth + 2);
    try {
        switch (h.id) {
            case 0x201: {
                std::uint32_t bone_count = r.read_u32();
                std::printf(". boneCount=%u, then %u reserved bytes\n",
                            bone_count, h.payload_size - 4u);
                break;
            }
            case 0x203: {
                std::printf(". name=\"%s\"\n", r.read_cstring().c_str());
                break;
            }
            case 0x205:
            case 0x206: {
                std::uint32_t parent = r.read_u32();
                std::uint32_t visible = r.read_u32();
                if (h.id == 0x206) {
                    std::uint32_t billboard = r.read_u32();
                    std::printf(". parent=%u visible=%u billboard=%u (matrix omitted)\n",
                                parent, visible, billboard);
                } else {
                    std::printf(". parent=%u visible=%u (matrix omitted)\n", parent, visible);
                }
                break;
            }
            case 0x401:
            case 0x1301:
            case 0x10101:
                std::printf(". \"%s\"\n", r.read_cstring().c_str());
                break;
            case 0x402: {
                // Layout (per AloImporter-1.2/3dsmax9/alamo2max.ms:546-550):
                //   u32 materialCount, 7 floats (bbox + unused),
                //   u32 isHidden, u32 isCollisionMesh, [88 bytes reserved]
                std::uint32_t mat_count = r.read_u32();
                r.skip(7 * 4);
                std::uint32_t hidden = r.read_u32();
                std::uint32_t collision = r.read_u32();
                std::printf(". materialCount=%u hidden=%u collision=%u (bbox + %zu reserved bytes omitted)\n",
                            mat_count, hidden, collision,
                            static_cast<std::size_t>(h.payload_size) - 40);
                break;
            }
            case 0x10001: {
                std::uint32_t verts = r.read_u32();
                std::uint32_t faces = r.read_u32();
                std::printf(". vertexCount=%u faceCount=%u\n", verts, faces);
                break;
            }
            case 0x10005:
            case 0x10007: {
                std::printf(". vertex data (%u bytes)\n", h.payload_size);
                break;
            }
            case 0x10004: {
                std::printf(". %u face indices (%u triangles)\n",
                            h.payload_size / 2, h.payload_size / 6);
                break;
            }
            case 0x1302: {
                std::uint32_t type = r.read_u32();
                const char* tname = (type == 0) ? "Omni"
                                  : (type == 1) ? "Directional"
                                  : (type == 2) ? "Spot" : "?";
                float r_ = r.read_f32(), g_ = r.read_f32(), b_ = r.read_f32();
                float intensity = r.read_f32();
                std::printf(". type=%s color=(%.3f,%.3f,%.3f) intensity=%.3f\n",
                            tname, r_, g_, b_, intensity);
                break;
            }
            default:
                std::printf(". (no decoder)\n");
                break;
        }
    } catch (const std::exception& e) {
        std::printf(". DECODE ERROR: %s\n", e.what());
    }
}

void walk(ChunkReader& r, int depth, const Options& opts, const std::uint8_t* file_base) {
    if (opts.max_depth >= 0 && depth > opts.max_depth) return;
    while (!r.eof()) {
        const std::size_t hdr_off_in_subreader = r.cursor();
        ChunkHeader h = r.read_header();
        // Translate the offset reported by the subreader back to a file-global offset
        // by computing the address delta from file_base. (Offsets in `h` are local to
        // r's data window, not file-global.)
        const std::size_t global_offset = static_cast<std::size_t>(
            (r.data() + hdr_off_in_subreader) - file_base);
        indent(depth);
        std::printf("[%8zu] 0x%08" PRIx32 "  %s  payload=%u  %s\n",
                    global_offset, h.id,
                    h.is_container ? "CONTAINER" : "leaf",
                    h.payload_size, chunk_label(h.id));
        if (h.is_container) {
            ChunkReader sub = r.subreader(h);
            walk(sub, depth + 1, opts, file_base);
            r.skip_payload(h);
        } else {
            if (!opts.brief) {
                // Pass the subreader's data pointer; h.payload_offset is local to it.
                decode_known_leaf(h, r.data(), depth);
            }
            r.skip_payload(h);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options opts = parse_args(argc, argv);
    try {
        auto data = read_file(opts.path);
        std::printf("%s  (%zu bytes)\n", opts.path.c_str(), data.size());
        if (data.empty()) {
            std::fprintf(stderr, "alo_dump: file is empty (no chunks)\n");
            return EXIT_FAILURE;
        }
        ChunkReader top(data.data(), data.size());
        walk(top, 0, opts, data.data());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "alo_dump: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

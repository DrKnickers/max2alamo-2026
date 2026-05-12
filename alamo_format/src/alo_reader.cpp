#include "alamo_format/chunk_tree.h"

#include "alamo_format/chunk_io.h"

#include <cstring>

namespace alamo_format {

namespace {

void parse_into(ChunkReader& r, std::vector<ChunkNode>& out) {
    while (!r.eof()) {
        ChunkHeader h = r.read_header();
        ChunkNode node;
        node.id = h.id;
        node.is_container = h.is_container;
        if (h.is_container) {
            ChunkReader sub = r.subreader(h);
            parse_into(sub, node.children);
            r.skip_payload(h);
        } else {
            node.payload.resize(h.payload_size);
            if (h.payload_size > 0) {
                std::memcpy(node.payload.data(),
                            r.data() + h.payload_offset,
                            h.payload_size);
            }
            r.skip_payload(h);
        }
        out.push_back(std::move(node));
    }
}

}  // namespace

std::vector<ChunkNode> read_chunk_tree(const std::uint8_t* data, std::size_t size) {
    ChunkReader r(data, size);
    std::vector<ChunkNode> roots;
    parse_into(r, roots);
    return roots;
}

}  // namespace alamo_format

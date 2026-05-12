#include "alamo_format/chunk_tree.h"

#include "alamo_format/chunk_io.h"

namespace alamo_format {

namespace {

void emit_nodes(ChunkWriter& w, const std::vector<ChunkNode>& nodes) {
    for (const auto& node : nodes) {
        auto h = w.begin_chunk(node.id, node.is_container);
        if (node.is_container) {
            emit_nodes(w, node.children);
        } else if (!node.payload.empty()) {
            w.write_bytes(node.payload.data(), node.payload.size());
        }
        w.end_chunk(h);
    }
}

}  // namespace

std::vector<std::uint8_t> write_chunk_tree(const std::vector<ChunkNode>& nodes) {
    ChunkWriter w;
    emit_nodes(w, nodes);
    return w.release();
}

}  // namespace alamo_format

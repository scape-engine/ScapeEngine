#include "graph_ids.h"

ed::PinId PinID::Encode(uint32_t node_id, uint32_t port_index, bool is_input) {
  // Pack: [is_input:1][port_index:15][node_id:16]
  uint32_t packed = (is_input ? 0x80000000 : 0) |
                    ((port_index & 0x7FFF) << 16) | (node_id & 0xFFFF);

  return ed::PinId(packed);
}

PinID PinID::Decode(ed::PinId id) {
  uint32_t packed = static_cast<uint32_t>(id.Get());
  PinID result;
  result.is_input = (packed & 0x80000000) != 0;
  result.port_index = (packed >> 16) & 0x7FFF;
  result.node_id = packed & 0xFFFF;

  return result;
}

ed::LinkId MakeLinkID(uint32_t src_node, uint32_t src_port, uint32_t dst_node,
                      uint32_t dst_port) {
  // Simple hash combining source and destination
  uint64_t id = (uint64_t(src_node) << 48) | (uint64_t(src_port) << 32) |
                (uint64_t(dst_node) << 16) | uint64_t(dst_port);

  return ed::LinkId(id);
}

LinkEnds DecodeLinkID(ed::LinkId id) {
  const uint64_t packed = id.Get();
  return {static_cast<uint32_t>(packed >> 48) & 0xFFFF,
          static_cast<uint32_t>(packed >> 32) & 0xFFFF,
          static_cast<uint32_t>(packed >> 16) & 0xFFFF,
          static_cast<uint32_t>(packed) & 0xFFFF};
}

#ifndef GRAPH_IDS_H
#define GRAPH_IDS_H

#include <cstdint>

#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

// A pin's id from its node and port (node ids start at 1: 0 is no id)
struct PinID {
  uint32_t node_id;
  uint32_t port_index;
  bool is_input;

  static ed::PinId Encode(uint32_t node_id, uint32_t port_index, bool is_input);
  static PinID Decode(ed::PinId id);
};

ed::LinkId MakeLinkID(uint32_t src_node, uint32_t src_port, uint32_t dst_node,
                      uint32_t dst_port);

// A link's pins, back from MakeLinkID
struct LinkEnds {
  uint32_t src_node, src_port, dst_node, dst_port;
};
LinkEnds DecodeLinkID(ed::LinkId id);

#endif  // GRAPH_IDS_H

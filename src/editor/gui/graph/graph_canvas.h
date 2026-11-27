#ifndef GRAPH_CANVAS_H
#define GRAPH_CANVAS_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

// A node graph's canvas, as Unreal's graph editors: the right mouse pans
// (and opens menus), the wheel zooms within limits, a zoom readout sits in
// the corner. Nodes, pins and links go between Begin and End (ed:: calls
// and GraphNode); colours are the editor's (EditorColor)
class GraphCanvas {
public:
  GraphCanvas();
  ~GraphCanvas();
  GraphCanvas(const GraphCanvas&) = delete;
  GraphCanvas& operator=(const GraphCanvas&) = delete;

  // False when there's no room to draw (then no End)
  bool Begin(const char* id);
  void End();

  // Frames every node, once they've been drawn and measured (nodes placed
  // this frame have no size yet)
  void FrameContent() { frame_in_ = 2; }

private:
  ed::EditorContext* context_ = nullptr;
  ImVec2 origin_;
  ImVec2 size_;
  int frame_in_ = 0;  // frames until the content is framed; 0: none asked
};

// A node as Unreal draws one: a title bar in `header` colour over the body,
// input pins on its left edge, outputs on its right
namespace GraphNode {

// The node's content is `width` wide; the title bar holds what comes before
// EndHeader
void Begin(ed::NodeId id, float width);
void EndHeader(ImU32 header);
void End();

// A pin: a circle, filled once linked, and its label beside it
void InputPin(ed::PinId id, const char* label, bool linked);
// At the node's right edge, after what is on its row (SameLine first)
void OutputPin(ed::PinId id, bool linked);

}  // namespace GraphNode

#endif  // GRAPH_CANVAS_H

#ifndef PCG_GRAPH_PANEL_H
#define PCG_GRAPH_PANEL_H

#include <cstdint>
#include <string>

#include <boost/signals2/connection.hpp>

#include "editor/gui/graph/graph_canvas.h"
#include "editor/gui/window_base.h"
#include "engine/pcg/graph/program_graph.h"

// A PCG graph asset (.pcggraph) as Unreal's PCG graph editor: nodes with
// their inputs on the left and outputs on the right, right-click to add
// nodes, Save writes the asset and reloads the generators using it. Opened
// from a Graph generator's Inspector or the asset browser
// (EditorEvents::open_pcg_graph), in the bottom panels while one is open
// TODO: the engine's node library holds only noise nodes (graph/nodes: the
// input, output and math files are empty, unregistered), so no graph
// compiles yet: "Graph has no output nodes"
// TODO: a Graph volume makes nothing yet: it should evaluate its graph into
// a heightfield mesh (PCGEngine::Update)
// TODO: node previews showing the graph's output, once it makes some
// TODO: a details panel (Unreal's) for the selected node; search in the
// add-node menu; undo
class PCGGraphPanel : public WindowBase {
public:
  PCGGraphPanel();

  void Render() override;

  bool IsOpen() const { return !path_.empty(); }
  // At once when saved; with unsaved edits it asks first
  void Close();

private:
  // What the open menu is for
  enum class Menu { AddNode, Node, Choice };

  // `graph`: project-relative
  void Open(const std::string& graph);
  bool Save();
  // After an edit: unsaved, and whether it still compiles
  void Edited();

  void DrawToolbar();
  void DrawNode(PCG::ProgramGraph::Node& node);
  void DrawParams(PCG::ProgramGraph::Node& node);
  void DrawLinks();
  void HandleCreate();
  void HandleDelete();
  void HandleMenus();
  void DrawClosePrompt();

  void AddNode(uint32_t type_id);

  GraphCanvas canvas_;
  std::string path_;  // project-relative; empty: none open
  PCG::ProgramGraph graph_;
  bool dirty_ = false;
  std::string error_;  // why the graph doesn't compile (or save)
  bool place_ = false;  // nodes go to their saved places on the next frame
  bool close_asked_ = false;
  Menu menu_ = Menu::AddNode;
  ImVec2 menu_at_;  // where a node added from the menu goes, canvas space
  ed::NodeId menu_node_;
  size_t menu_param_ = 0;  // the enum parameter the menu chooses for
  boost::signals2::scoped_connection opened_;
};

#endif  // PCG_GRAPH_PANEL_H

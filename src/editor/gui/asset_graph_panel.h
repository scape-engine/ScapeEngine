#ifndef ASSET_GRAPH_PANEL_H
#define ASSET_GRAPH_PANEL_H

#include <cstddef>
#include <string>

#include <boost/signals2/connection.hpp>

#include "editor/gui/graph/graph_canvas.h"
#include "editor/gui/window_base.h"
#include "engine/pcg/procmodel/descriptor/descriptor_edits.h"

// A procedural model's descriptor as a graph, as Unreal's asset graphs: a
// node per selection group, its parts as output pins, a link from a part to
// each group it activates. Edits change the descriptor's JSON in place
// (DescriptorEdits), so what the graph doesn't show is kept, and Save
// writes it back. Opened from a model's preview (EditorEvents::
// open_asset_graph), in the bottom panels while a descriptor is open
// TODO: a details panel (Unreal's) for what the nodes don't show: the
// model's path, scale and textures, a group's locators, lists, scale and
// deformation ranges, locator operations, constraints and bindings
// TODO: saved descriptors are written with nlohmann's layout (every array
// on its own lines), not the author's
// TODO: undo
class AssetGraphPanel : public WindowBase {
public:
  AssetGraphPanel();

  void Render() override;

  bool IsOpen() const { return !path_.empty(); }
  // At once when saved; with unsaved edits it asks first
  void Close();

private:
  using Document = ProcModel::DescriptorEdits::Document;

  // `descriptor`: project-relative
  void Open(const std::string& descriptor);
  bool Save();
  // After an edit: unsaved, and laid out again when groups came or went
  void Edited(bool changed, bool relayout = false);

  void DrawToolbar();
  void DrawNode(size_t group);
  void DrawLinks();
  void HandleCreate();
  void HandleDelete();
  void HandleMenus();
  void DrawClosePrompt();
  // Groups as a tree from their first parent, left to right, each node's
  // measured height apart
  void Layout();

  // Whether some group is activated by the part
  bool Activates(const std::string& part_id) const;

  GraphCanvas canvas_;
  std::string path_;  // project-relative; empty: none open
  Document doc_;
  bool dirty_ = false;
  std::string error_;      // why the descriptor doesn't parse (or save)
  int layout_in_ = 0;      // frames until laid out; nodes are measured first
  bool close_asked_ = false;
  // Where a group added from the menu goes, in canvas space
  ImVec2 menu_at_;
  ed::NodeId menu_node_;
  boost::signals2::scoped_connection opened_;
};

#endif  // ASSET_GRAPH_PANEL_H

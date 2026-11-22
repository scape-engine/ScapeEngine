#ifndef PCG_GRAPH_EDITOR_H
#define PCG_GRAPH_EDITOR_H

#include <imgui-node-editor/imgui_node_editor.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <boost/signals2/connection.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// ! should not include PCG directly
#include "engine/renderer/material/terrain_material.h"
#include "engine/pcg/graph/node_types.h"
#include "engine/pcg/graph/program_graph.h"

#include "editor/gui/window_base.h"

namespace ed = ax::NodeEditor;

//
// PIN/LINK ID HELPERS
//
struct PinID {
  uint32_t node_id;
  uint32_t port_index;
  bool is_input;

  static ed::PinId Encode(uint32_t node_id, uint32_t port_index, bool is_input);
  static PinID Decode(ed::PinId id);
};

ed::LinkId MakeLinkID(uint32_t src_node, uint32_t src_port, uint32_t dst_node,
                      uint32_t dst_port);

//
// NODE PREVIEW
//
struct NodePreview {
  static constexpr int kPreviewSize = 64;

  uint32_t texture_id;
  std::vector<uint8_t> pixels;
  bool dirty = true;

  // Preview transform (pan/zoom via Ctrl+drag/wheel)
  glm::vec2 offset = {0.0f, 0.0f};
  float scale = 1.0f;

  NodePreview() : pixels(kPreviewSize * kPreviewSize) {}

  void MarkDirty() { dirty = true; }
};

//
// PCG GRAPH EDITOR PANEL
//
class PCGGraphEditorPanel : public WindowBase {
public:
  PCGGraphEditorPanel();
  ~PCGGraphEditorPanel() { s_instance_ = nullptr; }

  static PCGGraphEditorPanel* Get() { return s_instance_; }

  void Render() override;
  void SetGraph(PCG::ProgramGraph* graph);

  // Callbacks
  std::function<void()> on_graph_changed_;
  std::function<void(uint32_t)> on_node_selected_;
  std::function<void()> on_regenerate_requested_;

  // State control
  void OpenGraph(ResourceID generator_id, PCG::ProgramGraph* graph);
  void CloseGraph();

  // Queries
  bool IsOpen() const { return show_editor_; }
  ResourceID GetActiveGeneratorId() const { return active_generator_id_; }

private:
  // Editor context
  ed::EditorContext* context_;

  // Opens generators from EditorEvents::open_graph_editor
  boost::signals2::scoped_connection open_connection_;
  // Set when the open generator has properties but no graph (Region)
  bool show_properties_ = false;

  // Reference to graph being edited
  PCG::ProgramGraph* graph_;

  // Node previews (keyed by node_id)
  std::unordered_map<uint32_t, NodePreview> previews_;

  // Selection state
  std::vector<uint32_t> selected_node_ids_;
  uint32_t inspected_node_id_;

  // UI state
  bool show_node_picker_ = false;
  ImVec2 node_picker_position_ = {0, 0};
  ImVec2 context_menu_position_ = {0, 0};
  bool show_context_menu_ = false;

  // Editor state
  bool show_editor_;
  ResourceID active_generator_id_;

  // Preview settings
  enum class PreviewMode { SliceXY, SliceXZ };
  PreviewMode preview_mode_ = PreviewMode::SliceXZ;
  float preview_update_timer_ = 0.0f;
  bool live_update_enabled_ = true;

  // Styling
  ImColor color_input_pin_ = ImColor(0.4f, 0.4f, 1.0f);
  ImColor color_output_pin_ = ImColor(0.4f, 1.0f, 0.4f);
  ImColor color_link_ = ImColor(0.6f, 0.6f, 0.8f);

  static PCGGraphEditorPanel* s_instance_;

  //
  // DRAW FUNCTIONS
  //
  void DrawToolbar();
  void DrawRegionProperties();
  void DrawPlanetProperties();
  void DrawCraftProperties();
  // Tile colours and material settings; they apply without a rebuild
  void DrawTerrainLook(TerrainMaterialData& look);
  // Marks the volumes using the open generator for a rebuild
  void RequestRebuild();
  void DrawNodes();
  void DrawInlineParams(PCG::ProgramGraph::Node& node,
                        const PCG::NodeType& type);
  void DrawLinks();

  //
  // NODE INTERACTION
  //
  void HandleLinkInteraction();
  void HandleContextMenu();
  void DrawNodePicker();  // TODO: handle search properly (closes on click)

  //
  //  NODE PREVIEW
  //
  bool HasPreview(uint32_t type_id) const;
  void DrawNodePreview(uint32_t node_id);
  void UpdateNodePreview(uint32_t node_id, NodePreview& preview);

  //
  //  PREVIEW SCHEDULING
  //
  void SchedulePreviewUpdate();
  void ProcessPreviewTimer(float dt);
  void UpdatePreviews();

  //
  // HANDLE SELECTION
  //
  void UpdateSelection();
  void DeleteSelectedNodes();

  //
  //  NODE CREATION
  //
  void CreateNode(uint32_t type_id, ImVec2 screen_pos);

  //
  //  CHANGE NOTIFICATIONS
  //
  void OnGraphChanged();
  void OnNodeParamChanged(uint32_t node_id);

  //
  //  HELPERS
  //
  ImColor GetCategoryColor(PCG::Category cat) const;
  std::string GetCategoryName(PCG::Category cat) const;
};

#endif  // PCG_GRAPH_EDITOR_H
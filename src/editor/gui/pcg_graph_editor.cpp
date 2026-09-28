#include "pcg_graph_editor.h"

#include <algorithm>
#include <string>

#include "engine/core/logger.h"
#include "engine/memory/resource.h"

namespace ed = ax::NodeEditor;

PCGGraphEditorPanel* PCGGraphEditorPanel::s_instance_ = nullptr;

//
// PIN/LINK ID HELPERS
//
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

//
// PCG GRAPH EDITOR PANEL
//
PCGGraphEditorPanel::PCGGraphEditorPanel()
    : context_(nullptr),
      graph_(nullptr),
      inspected_node_id_(0),
      show_editor_(false),
      active_generator_id_(0) {
  ed::Config config;
  config.SettingsFile = nullptr;
  context_ = ed::CreateEditor(&config);
  Logger::getInstance().Log(LogLevel::Info, "PCGGraphEditorPanel: initialized");
}

void PCGGraphEditorPanel::SetGraph(PCG::ProgramGraph* graph) {
  graph_ = graph;
  previews_.clear();
  selected_node_ids_.clear();
  inspected_node_id_ = 0;
  SchedulePreviewUpdate();
  Logger::getInstance().Log(LogLevel::Info, "PCGGraphEditorPanel: graph set");
}

void PCGGraphEditorPanel::OpenGraph(ResourceID generator_id,
                                    PCG::ProgramGraph* graph) {
  active_generator_id_ = generator_id;
  SetGraph(graph);
  show_editor_ = true;
}

void PCGGraphEditorPanel::CloseGraph() {
  show_editor_ = false;
  active_generator_id_ = 0;
  graph_ = nullptr;
}

//
//  MAIN DRAW FUNCTION
//
void PCGGraphEditorPanel::Render() {
  // Only show when explicitly opened with a graph
  if (!show_editor_ || !graph_) {
    return;
  }

  ImGui::Begin("PCG Graph Editor", &show_editor_, ImGuiWindowFlags_MenuBar);

  if (!context_) {
    ImGui::TextDisabled("Editor context not initialized");
    ImGui::End();
    return;
  }

  // TODO: Use time.h not imgui's dt
  float dt = ImGui::GetIO().DeltaTime;

  ProcessPreviewTimer(dt);
  DrawToolbar();

  ImGui::Separator();

  // Begin node editor
  ed::SetCurrentEditor(context_);
  ed::Begin("PCG Graph Editor");

  // Draw all nodes
  DrawNodes();

  // Draw all connections
  DrawLinks();

  HandleLinkInteraction();
  HandleContextMenu();

  ed::End();

  // Update selection state
  UpdateSelection();

  ed::SetCurrentEditor(nullptr);

  ImGui::End();

  if (!show_editor_) {
    CloseGraph();
  }
}

//
//  TOOLBAR
//
void PCGGraphEditorPanel::DrawToolbar() {
  if (ImGui::BeginMenuBar())
    return;

  if (ImGui::BeginMenu("Graph")) {
    if (ImGui::MenuItem("Update Previews"))
      UpdatePreviews();

    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Debug")) {
    if (ImGui::MenuItem("Update Previews", "F5"))
      UpdatePreviews();

    ImGui::MenuItem("Live Update", nullptr, &live_update_enabled_);
    ImGui::Separator();
    if (ImGui::BeginMenu("Preview Axes")) {
      if (ImGui::MenuItem("XY", nullptr,
                          preview_mode_ == PreviewMode::SliceXY)) {
        preview_mode_ = PreviewMode::SliceXY;
        SchedulePreviewUpdate();
      }
      if (ImGui::MenuItem("XZ", nullptr,
                          preview_mode_ == PreviewMode::SliceXZ)) {
        preview_mode_ = PreviewMode::SliceXZ;
        SchedulePreviewUpdate();
      }
      ImGui::EndMenu();
    }
    ImGui::EndMenu();
  }

  ImGui::EndMenuBar();
}

//
//  NODE RENDERING
//
void PCGGraphEditorPanel::DrawNodes() {
  const auto& type_db = PCG::NodeTypeDB::Instance();

  graph_->ForEachNode([&](PCG::ProgramGraph::Node& node) {
    const auto& node_type = type_db.Get(node.type_id);

    ed::BeginNode(node.id);

    // Title bar
    ImGui::PushStyleColor(ImGuiCol_Text,
                          (ImU32)GetCategoryColor(node_type.category));
    ImGui::TextUnformatted(node_type.name.c_str());
    ImGui::PopStyleColor();

    // Show custom name if set
    if (!node.name.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("(%s)", node.name.c_str());
    }

    ImGui::Separator();

    // Layout: inputs on left, outputs on right
    const size_t row_count = std::max(node.inputs.size(), node.outputs.size());

    for (size_t row = 0; row < row_count; ++row) {
      const bool has_input = row < node.inputs.size();
      const bool has_output = row < node.outputs.size();

      // Input pin
      if (has_input) {
        ed::BeginPin(PinID::Encode(node.id, row, true), ed::PinKind::Input);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ">");
        ImGui::SameLine();
        ImGui::TextUnformatted(node_type.inputs[row].name.c_str());

        // Show default value hint if not connected
        if (node.inputs[row].connections.empty()) {
          ImGui::SameLine();
          ImGui::TextDisabled(": %.2f", node.default_inputs[row]);
        }
        ed::EndPin();
      }

      // Spacer between input and output
      if (has_input && has_output) {
        ImGui::SameLine(150.0f);  // Fixed width for alignment
      } else if (has_output && !has_input) {
        ImGui::Dummy(ImVec2(150.0f, 0));
        ImGui::SameLine();
      }

      // Output pin
      if (has_output) {
        ed::BeginPin(PinID::Encode(node.id, row, false), ed::PinKind::Output);
        ImGui::TextUnformatted(node_type.outputs[row].name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ">");
        ed::EndPin();
      }
    }

    DrawInlineParams(node, node_type);

    // Preview image
    if (HasPreview(node.type_id)) {
      DrawNodePreview(node.id);
    }

    ed::EndNode();

    // Store position back to graph
    ImVec2 pos = ed::GetNodePosition(node.id);
    node.gui_position = glm::vec2(pos.x, pos.y);
  });
}

//
//  INLINE PARAMETER EDITORS
//
void PCGGraphEditorPanel::DrawInlineParams(PCG::ProgramGraph::Node& node,
                                           const PCG::NodeType& type) {
  ImGui::PushItemWidth(100.0f);

  for (size_t i = 0; i < type.params.size(); ++i) {
    const auto& param = type.params[i];

    ImGui::PushID(static_cast<int>(i));

    switch (param.type) {
      case PCG::NodeType::Param::Type::Float: {
        float value = std::get<float>(node.params[i]);
        if (param.has_range) {
          if (ImGui::SliderFloat(param.name.c_str(), &value, param.min_value,
                                 param.max_value)) {
            node.params[i] = value;
            OnNodeParamChanged(node.id);
          }
        } else {
          if (ImGui::DragFloat(param.name.c_str(), &value, 0.01f)) {
            node.params[i] = value;
            OnNodeParamChanged(node.id);
          }
        }
        break;
      }

      case PCG::NodeType::Param::Type::Int: {
        int value = std::get<int>(node.params[i]);
        if (param.has_range) {
          if (ImGui::SliderInt(param.name.c_str(), &value,
                               static_cast<int>(param.min_value),
                               static_cast<int>(param.max_value))) {
            node.params[i] = value;
            OnNodeParamChanged(node.id);
          }
        } else {
          if (ImGui::DragInt(param.name.c_str(), &value)) {
            node.params[i] = value;
            OnNodeParamChanged(node.id);
          }
        }
        break;
      }

      case PCG::NodeType::Param::Type::Bool: {
        bool value = std::get<bool>(node.params[i]);
        if (IMComponents::Checkbox(param.name.c_str(), &value)) {
          node.params[i] = value;
          OnNodeParamChanged(node.id);
        }
        break;
      }

      case PCG::NodeType::Param::Type::Enum: {
        // Combo box for enum values
        std::string current = std::get<std::string>(node.params[i]);
        if (ImGui::BeginCombo(param.name.c_str(), current.c_str())) {
          for (const auto& option : param.enum_items) {
            bool selected = (option == current);
            if (ImGui::Selectable(option.c_str(), selected)) {
              node.params[i] = option;
              OnNodeParamChanged(node.id);
            }
          }
          ImGui::EndCombo();
        }
        break;
      }
    }

    ImGui::PopID();
  }

  ImGui::PopItemWidth();
}

//
//  LINK RENDERING & INTERACTION
//
void PCGGraphEditorPanel::DrawLinks() {
  std::vector<PCG::ProgramGraph::Connection> connections;
  graph_->GetConnections(connections);

  for (const auto& conn : connections) {
    ed::LinkId link_id = MakeLinkID(conn.src.node_id, conn.src.port_index,
                                    conn.dst.node_id, conn.dst.port_index);
    ed::PinId src_pin =
        PinID::Encode(conn.src.node_id, conn.src.port_index, false);
    ed::PinId dst_pin =
        PinID::Encode(conn.dst.node_id, conn.dst.port_index, true);

    ed::Link(link_id, src_pin, dst_pin, color_link_, 2.0f);
  }
}

void PCGGraphEditorPanel::HandleLinkInteraction() {
  // Handle new connection requests
  if (ed::BeginCreate()) {
    ed::PinId start_pin_id, end_pin_id;
    if (ed::QueryNewLink(&start_pin_id, &end_pin_id)) {
      if (start_pin_id && end_pin_id) {
        PinID start = PinID::Decode(start_pin_id);
        PinID end = PinID::Decode(end_pin_id);

        // Ensure we have output -> input
        if (start.is_input)
          std::swap(start, end);

        if (!start.is_input && end.is_input) {
          // Validate connection
          PCG::ProgramGraph::PortLocation src{start.node_id, start.port_index};
          PCG::ProgramGraph::PortLocation dst{end.node_id, end.port_index};

          if (graph_->CanConnect(src, dst)) {
            if (ed::AcceptNewItem(ImColor(0.4f, 1.0f, 0.4f), 3.0f)) {
              // Remove existing connection to this input (if any)
              auto& dst_node = graph_->GetNode(dst.node_id);
              if (!dst_node.inputs[dst.port_index].connections.empty()) {
                auto existing = dst_node.inputs[dst.port_index].connections[0];
                graph_->Disconnect(existing, dst);
              }

              graph_->Connect(src, dst);
              OnGraphChanged();
            }
          } else {
            ed::RejectNewItem(ImColor(1.0f, 0.2f, 0.2f), 2.0f);
          }
        }
      }
    }
  }
  ed::EndCreate();

  // Handle link deletion
  if (ed::BeginDelete()) {
    ed::LinkId deleted_link_id;
    while (ed::QueryDeletedLink(&deleted_link_id)) {
      if (ed::AcceptDeletedItem()) {
        // Decode link ID to find connection
        // (simplified - in production, maintain a link ID -> connection map)
        std::vector<PCG::ProgramGraph::Connection> connections;
        graph_->GetConnections(connections);

        for (const auto& conn : connections) {
          ed::LinkId id = MakeLinkID(conn.src.node_id, conn.src.port_index,
                                     conn.dst.node_id, conn.dst.port_index);
          if (id == deleted_link_id) {
            graph_->Disconnect(conn.src, conn.dst);
            OnGraphChanged();
            break;
          }
        }
      }
    }

    // Handle node deletion
    ed::NodeId deleted_node_id;
    while (ed::QueryDeletedNode(&deleted_node_id)) {
      if (ed::AcceptDeletedItem()) {
        uint32_t node_id = static_cast<uint32_t>(deleted_node_id.Get());
        graph_->RemoveNode(node_id);
        previews_.erase(node_id);
        OnGraphChanged();
      }
    }
  }
  ed::EndDelete();
}

//
//  CONTEXT MENU
//
void PCGGraphEditorPanel::HandleContextMenu() {
  ed::Suspend();

  // Background right-click
  if (ed::ShowBackgroundContextMenu()) {
    ImGui::OpenPopup("GraphContextMenu");
    context_menu_position_ = ImGui::GetMousePos();
  }

  if (ImGui::BeginPopup("GraphContextMenu")) {
    if (ImGui::MenuItem("Add Node")) {
      show_node_picker_ = true;
      node_picker_position_ = context_menu_position_;
    }

    if (!selected_node_ids_.empty()) {
      if (ImGui::MenuItem("Delete Selected", "Del")) {
        DeleteSelectedNodes();
      }
    }

    ImGui::EndPopup();
  }

  DrawNodePicker();

  ed::Resume();
}

//
//  NODE PICKER DIALOG
// TODO: handle search properly (closes on click)
//
void PCGGraphEditorPanel::DrawNodePicker() {
  if (!show_node_picker_)
    return;

  ImGui::SetNextWindowPos(node_picker_position_, ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);

  // Open popup if we just started showing it
  static bool popup_opened = false;
  if (!popup_opened) {
    ImGui::OpenPopup("##NodePickerPopup");
    popup_opened = true;
  }

  if (ImGui::BeginPopup("##NodePickerPopup",
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
    static char search_buffer[256] = "";
    ImGui::InputTextWithHint("##search", "Search...", search_buffer, 256);

    ImGui::Separator();

    const auto& type_db = PCG::NodeTypeDB::Instance();
    std::string search_lower = search_buffer;
    std::transform(search_lower.begin(), search_lower.end(),
                   search_lower.begin(), ::tolower);

    // Group by category
    for (int cat = 0; cat < static_cast<int>(PCG::Category::COUNT); ++cat) {
      auto category = static_cast<PCG::Category>(cat);
      std::string category_name = GetCategoryName(category);

      bool has_matches = false;

      type_db.ForEachType([&](uint32_t type_id, const PCG::NodeType& type) {
        if (type.category != category)
          return;

        std::string name_lower = type.name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                       ::tolower);

        if (search_lower.empty() ||
            name_lower.find(search_lower) != std::string::npos) {
          has_matches = true;
        }
      });

      if (!has_matches)
        continue;

      if (ImGui::CollapsingHeader(category_name.c_str(),
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        type_db.ForEachType([&](uint32_t type_id, const PCG::NodeType& type) {
          if (type.category != category)
            return;

          std::string name_lower = type.name;
          std::transform(name_lower.begin(), name_lower.end(),
                         name_lower.begin(), ::tolower);

          if (!search_lower.empty() &&
              name_lower.find(search_lower) == std::string::npos) {
            return;
          }

          if (ImGui::Selectable(type.name.c_str())) {
            CreateNode(type_id, node_picker_position_);
            show_node_picker_ = false;
            popup_opened = false;
            search_buffer[0] = '\0';
            ImGui::CloseCurrentPopup();
          }
        });
      }
    }

    // Close on escape or click outside
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
         ImGui::IsMouseClicked(0))) {
      show_node_picker_ = false;
      popup_opened = false;
      search_buffer[0] = '\0';
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  } else {
    // Popup was closed externally
    show_node_picker_ = false;
    popup_opened = false;
  }
}

//
//  NODE PREVIEW
//
bool PCGGraphEditorPanel::HasPreview(uint32_t type_id) const {
  // Preview nodes like SDF Preview, or outputs
  return type_id == static_cast<uint32_t>(PCG::NodeTypeID::Preview) ||
         type_id == static_cast<uint32_t>(PCG::NodeTypeID::OutputHeight);
}

void PCGGraphEditorPanel::DrawNodePreview(uint32_t node_id) {
  auto& preview = previews_[node_id];

  // Update texture if dirty
  if (preview.dirty) {
    UpdateNodePreview(node_id, preview);
    preview.dirty = false;
  }

  // Draw preview image
  if (preview.texture_id != 0) {
    ImGui::Image((ImTextureID)(uintptr_t)preview.texture_id,
                 ImVec2(NodePreview::kPreviewSize, NodePreview::kPreviewSize));

    // Handle Ctrl+drag to pan preview
    if (ImGui::IsItemHovered()) {
      ImGuiIO& io = ImGui::GetIO();
      if (io.KeyCtrl && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        preview.offset.x -= io.MouseDelta.x * preview.scale;
        preview.offset.y += io.MouseDelta.y * preview.scale;
        preview.MarkDirty();
      }
      // Handle Ctrl+wheel to zoom
      if (io.KeyCtrl && io.MouseWheel != 0) {
        float zoom_factor = io.MouseWheel > 0 ? 0.9f : 1.1f;
        preview.scale *= zoom_factor;
        preview.MarkDirty();
      }
    }
  }
}

void PCGGraphEditorPanel::UpdateNodePreview(uint32_t node_id,
                                            NodePreview& preview) {
  // Get connection to preview node's input
  auto* node = graph_->TryGetNode(node_id);
  if (!node || node->inputs.empty() || node->inputs[0].connections.empty()) {
    return;
  }

  // TODO: Evaluate graph at each pixel position
  // This would use your Generator evaluation pipeline
  const float scale = preview.scale;
  const glm::vec2 offset = preview.offset;

  for (int y = 0; y < NodePreview::kPreviewSize; ++y) {
    for (int x = 0; x < NodePreview::kPreviewSize; ++x) {
      // Compute world position based on preview mode
      float wx = (x - NodePreview::kPreviewSize / 2) * scale + offset.x;
      float wy = (y - NodePreview::kPreviewSize / 2) * scale + offset.y;

      // Evaluate graph at this position
      // float value = EvaluateGraphAt(node_id, wx, wy);
      float value = 0.0f;  // Placeholder

      // Map [-1, 1] to [0, 255]
      uint8_t pixel = static_cast<uint8_t>(
          std::clamp((value * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
      preview.pixels[y * NodePreview::kPreviewSize + x] = pixel;
    }
  }

  // TODO: Upload pixels to texture (BGFX texture update)
  // preview.texture_id = CreateOrUpdateTexture(preview.pixels);
}

//
//  PREVIEW UPDATE SCHEDULING
//
void PCGGraphEditorPanel::SchedulePreviewUpdate() {
  preview_update_timer_ = 0.5f;  // Delay to batch rapid changes
}

void PCGGraphEditorPanel::ProcessPreviewTimer(float dt) {
  if (preview_update_timer_ > 0.0f) {
    preview_update_timer_ -= dt;
    if (preview_update_timer_ <= 0.0f) {
      UpdatePreviews();
    }
  }
}

void PCGGraphEditorPanel::UpdatePreviews() {
  for (auto& [node_id, preview] : previews_) {
    preview.MarkDirty();
  }

  if (live_update_enabled_ && on_regenerate_requested_) {
    on_regenerate_requested_();
  }
}

//
//  SELECTION HANDLING
//
void PCGGraphEditorPanel::UpdateSelection() {
  selected_node_ids_.clear();

  int selected_count = ed::GetSelectedObjectCount();
  if (selected_count > 0) {
    std::vector<ed::NodeId> selected_nodes(selected_count);
    int node_count =
        ed::GetSelectedNodes(selected_nodes.data(), selected_count);

    for (int i = 0; i < node_count; ++i) {
      selected_node_ids_.push_back(
          static_cast<uint32_t>(selected_nodes[i].Get()));
    }

    if (!selected_node_ids_.empty()) {
      inspected_node_id_ = selected_node_ids_.back();
      if (on_node_selected_) {
        on_node_selected_(inspected_node_id_);
      }
    }
  }
}

void PCGGraphEditorPanel::DeleteSelectedNodes() {
  for (uint32_t node_id : selected_node_ids_) {
    graph_->RemoveNode(node_id);
    previews_.erase(node_id);
  }
  selected_node_ids_.clear();
  OnGraphChanged();
}

// ─────────────────────────────────────────────────────────────────────────
//  NODE CREATION
// ─────────────────────────────────────────────────────────────────────────
void PCGGraphEditorPanel::CreateNode(uint32_t type_id, ImVec2 screen_pos) {
  // Convert screen position to canvas position
  ImVec2 canvas_pos = ed::ScreenToCanvas(screen_pos);

  uint32_t node_id = graph_->GenerateNodeId();

  const auto& type_db = PCG::NodeTypeDB::Instance();
  const auto& node_type = type_db.Get(type_id);

  auto* node = graph_->CreateNode(type_id, node_id);
  node->gui_position = glm::vec2(canvas_pos.x, canvas_pos.y);

  // Initialize inputs/outputs/params from type definition
  node->inputs.resize(node_type.inputs.size());
  node->outputs.resize(node_type.outputs.size());
  node->default_inputs.resize(node_type.inputs.size());
  node->params.resize(node_type.params.size());

  for (size_t i = 0; i < node_type.inputs.size(); ++i) {
    node->default_inputs[i] = node_type.inputs[i].default_value;
  }
  for (size_t i = 0; i < node_type.params.size(); ++i) {
    node->params[i] = node_type.params[i].default_value;
  }

  // Set node position in editor
  ed::SetNodePosition(node_id, canvas_pos);

  OnGraphChanged();
}

//
//  CHANGE NOTIFICATIONS
//
void PCGGraphEditorPanel::OnGraphChanged() {
  SchedulePreviewUpdate();
  if (on_graph_changed_) {
    on_graph_changed_();
  }
}

void PCGGraphEditorPanel::OnNodeParamChanged(uint32_t node_id) {
  if (previews_.count(node_id)) {
    previews_[node_id].MarkDirty();
  }
  SchedulePreviewUpdate();
}

//
//  HELPERS
//
ImColor PCGGraphEditorPanel::GetCategoryColor(PCG::Category cat) const {
  switch (cat) {
    case PCG::Category::INPUT:
      return ImColor(0.4f, 0.8f, 0.4f);
    case PCG::Category::OUTPUT:
      return ImColor(0.8f, 0.4f, 0.4f);
    case PCG::Category::MATH:
      return ImColor(0.6f, 0.6f, 0.9f);
    case PCG::Category::NOISE:
      return ImColor(0.9f, 0.7f, 0.3f);
    case PCG::Category::FILTER:
      return ImColor(0.7f, 0.5f, 0.8f);
    case PCG::Category::EROSION:
      return ImColor(0.5f, 0.7f, 0.7f);
    default:
      return ImColor(0.7f, 0.7f, 0.7f);
  }
}

std::string PCGGraphEditorPanel::GetCategoryName(PCG::Category cat) const {
  switch (cat) {
    case PCG::Category::INPUT:
      return "Input";
    case PCG::Category::OUTPUT:
      return "Output";
    case PCG::Category::MATH:
      return "Math";
    case PCG::Category::NOISE:
      return "Noise";
    case PCG::Category::FILTER:
      return "Filters";
    case PCG::Category::EROSION:
      return "Erosion";
    default:
      return "Other";
  }
}
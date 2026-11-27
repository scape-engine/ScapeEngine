#include "pcg_graph_panel.h"

#include <algorithm>
#include <vector>

#include "editor/events.h"
#include "editor/gui/components/generator_properties.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/graph/graph_ids.h"
#include "editor/gui/modals/popup_menu.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "engine/core/logger.h"
#include "engine/pcg/graph/graph_runtime.h"
#include "engine/pcg/graph/graph_serializer.h"
#include "engine/pcg/graph/node_types.h"

namespace {

constexpr float node_width = 220.0f;
constexpr float label_width = 84.0f;  // a parameter's name, before its field

const char* _CategoryName(PCG::Category category) {
  switch (category) {
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
      return "Utility";
  }
}

// Unreal tells node kinds apart by their title bar
ImU32 _Header(PCG::Category category) {
  switch (category) {
    case PCG::Category::INPUT:
      return EditorStyles::WithAlpha(EditorColor::success, 120);
    case PCG::Category::OUTPUT:
      return EditorStyles::WithAlpha(EditorColor::error, 200);
    case PCG::Category::NOISE:
      return EditorStyles::WithAlpha(EditorColor::orange_dim, 160);
    case PCG::Category::MATH:
      return EditorColor::accent_deep;
    default:
      return EditorColor::surface_4;
  }
}

}  // namespace

PCGGraphPanel::PCGGraphPanel() {
  opened_ = EditorEvents::open_pcg_graph.connect(
      [this](const std::string& graph) { Open(graph); });
}

void PCGGraphPanel::Open(const std::string& graph) {
  if (graph == path_)
    return;
  // Another graph replaces this one only once its edits are dealt with
  if (dirty_) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "[PCGGraph] Save or discard " + path_ + " before opening another");
    close_asked_ = true;
    return;
  }
  PCG::ProgramGraph loaded;
  if (!PCG::GraphSerializer::LoadFromFile(
          Runtime::Project().AbsolutePath(graph).string(), loaded)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[PCGGraph] Can't read " + graph);
    return;
  }
  path_ = graph;
  graph_.CopyFrom(loaded);
  dirty_ = close_asked_ = false;
  place_ = true;
  Edited();
  dirty_ = false;
}

void PCGGraphPanel::Close() {
  if (dirty_) {
    close_asked_ = true;
    return;
  }
  path_.clear();
  graph_.Clear();
  error_.clear();
}

bool PCGGraphPanel::Save() {
  if (!PCG::GraphSerializer::SaveToFile(
          graph_, Runtime::Project().AbsolutePath(path_).string())) {
    error_ = "Can't write " + path_;
    return false;
  }
  dirty_ = false;
  GeneratorProperties::ReloadGraph(path_);
  return true;
}

void PCGGraphPanel::Edited() {
  dirty_ = true;
  // The engine's own compiler says whether generators can run it
  PCG::GraphRuntime runtime;
  const PCG::CompilationResult result = runtime.Compile(graph_);
  error_ = result.success ? "" : result.message;
}

void PCGGraphPanel::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool visible = ImGui::Begin("PCG Graph", nullptr);
  ImGui::PopStyleVar();
  if (!visible || !IsOpen()) {
    ImGui::End();
    return;
  }
  DrawToolbar();
  if (canvas_.Begin("PCG Graph")) {
    if (place_) {
      graph_.ForEachNode([](const PCG::ProgramGraph::Node& node) {
        ed::SetNodePosition(node.id,
                            ImVec2(node.gui_position.x, node.gui_position.y));
      });
      place_ = false;
      canvas_.FrameContent();
    }
    std::vector<uint32_t> ids;
    graph_.GetNodeIds(ids);
    for (const uint32_t id : ids)
      DrawNode(graph_.GetNode(id));
    DrawLinks();
    HandleCreate();
    HandleDelete();
    HandleMenus();
    canvas_.End();
  }
  DrawClosePrompt();
  ImGui::End();
}

void PCGGraphPanel::DrawToolbar() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  ImGui::BeginChild("##toolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + 12.0f),
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  const bool focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  ImGui::BeginDisabled(!dirty_);
  if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save") ||
      (focused && dirty_ &&
       ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)))
    Save();
  ImGui::EndDisabled();
  IMComponents::Tooltip(
      "Write the graph (Ctrl+S); generators using it reload it");
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_EXPAND " Frame"))
    canvas_.FrameContent();
  IMComponents::Tooltip("Show the whole graph");

  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  IMComponents::Label(path_ + (dirty_ ? " *" : ""), EditorColor::text_dim);
  if (!error_.empty()) {
    ImGui::SameLine();
    IMComponents::Label(error_, EditorColor::console_error);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

void PCGGraphPanel::DrawNode(PCG::ProgramGraph::Node& node) {
  const PCG::NodeType& type = PCG::NodeTypeDB::Instance().Get(node.type_id);
  ImGui::PushID(static_cast<int>(node.id));
  GraphNode::Begin(node.id, node_width);

  ImGui::TextUnformatted(type.name.c_str());
  if (!node.name.empty()) {
    ImGui::SameLine();
    IMComponents::Label(node.name, EditorColor::text_dim);
  }
  GraphNode::EndHeader(_Header(type.category));

  // Inputs on the left, outputs on the right, row by row
  const size_t rows = std::max(node.inputs.size(), node.outputs.size());
  for (size_t row = 0; row < rows; ++row) {
    ImGui::PushID(static_cast<int>(row));
    if (row < node.inputs.size()) {
      const bool linked = !node.inputs[row].connections.empty();
      GraphNode::InputPin(PinID::Encode(node.id, row, true),
                          type.inputs[row].name.c_str(), linked);
      // An unlinked input takes its value here
      if (!linked) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(56.0f);
        if (ImGui::DragFloat("##default", &node.default_inputs[row], 0.01f,
                             0.0f, 0.0f, "%.2f"))
          Edited();
      }
    } else {
      ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    }
    if (row < node.outputs.size())
      GraphNode::OutputPin(PinID::Encode(node.id, row, false),
                           type.outputs[row].name.c_str(),
                           !node.outputs[row].connections.empty());
    ImGui::PopID();
  }
  DrawParams(node);

  GraphNode::End();
  ImGui::PopID();

  // Moving a node is an edit too: its place is saved
  const ImVec2 at = ed::GetNodePosition(node.id);
  if (!place_ && (at.x != node.gui_position.x || at.y != node.gui_position.y)) {
    node.gui_position = glm::vec2(at.x, at.y);
    dirty_ = true;
  }
}

void PCGGraphPanel::DrawParams(PCG::ProgramGraph::Node& node) {
  const PCG::NodeType& type = PCG::NodeTypeDB::Instance().Get(node.type_id);
  using Type = PCG::NodeType::Param::Type;
  for (size_t i = 0; i < type.params.size(); ++i) {
    const auto& param = type.params[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(param.name.c_str());
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(node_width - label_width);
    bool changed = false;
    switch (param.type) {
      case Type::Float: {
        float value = std::get<float>(node.params[i]);
        changed = param.has_range
                      ? ImGui::SliderFloat("##value", &value, param.min_value,
                                           param.max_value)
                      : ImGui::DragFloat("##value", &value, 0.01f);
        if (changed)
          node.params[i] = value;
        break;
      }
      case Type::Int: {
        int value = std::get<int>(node.params[i]);
        changed = param.has_range
                      ? ImGui::SliderInt("##value", &value,
                                         static_cast<int>(param.min_value),
                                         static_cast<int>(param.max_value))
                      : ImGui::DragInt("##value", &value);
        if (changed)
          node.params[i] = value;
        break;
      }
      case Type::Bool: {
        bool value = std::get<bool>(node.params[i]);
        changed = IMComponents::Checkbox("##value", &value);
        if (changed)
          node.params[i] = value;
        break;
      }
      case Type::Enum:
        // A combo's popup can't open inside the canvas: the menu chooses
        if (ImGui::Button(std::get<std::string>(node.params[i]).c_str(),
                          ImVec2(node_width - label_width, 0.0f))) {
          menu_ = Menu::Choice;
          menu_node_ = node.id;
          menu_param_ = i;
          PopupMenu::Open();
        }
        break;
    }
    if (changed)
      Edited();
    ImGui::PopID();
  }
}

void PCGGraphPanel::DrawLinks() {
  std::vector<PCG::ProgramGraph::Connection> connections;
  graph_.GetConnections(connections);
  for (const auto& link : connections)
    ed::Link(MakeLinkID(link.src.node_id, link.src.port_index,
                        link.dst.node_id, link.dst.port_index),
             PinID::Encode(link.src.node_id, link.src.port_index, false),
             PinID::Encode(link.dst.node_id, link.dst.port_index, true),
             ImGui::ColorConvertU32ToFloat4(EditorColor::accent_border), 2.0f);
}

void PCGGraphPanel::HandleCreate() {
  if (ed::BeginCreate(
          ImGui::ColorConvertU32ToFloat4(EditorColor::accent_border), 2.0f)) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b)) {
      PinID from = PinID::Decode(a), to = PinID::Decode(b);
      if (from.is_input)
        std::swap(from, to);
      const PCG::ProgramGraph::PortLocation src{from.node_id,
                                                from.port_index};
      const PCG::ProgramGraph::PortLocation dst{to.node_id, to.port_index};
      if (from.is_input || !to.is_input || !graph_.CanConnect(src, dst)) {
        ed::RejectNewItem(
            ImGui::ColorConvertU32ToFloat4(EditorColor::console_error), 2.0f);
      } else if (ed::AcceptNewItem()) {
        // An input takes one link: a new one replaces it
        auto& input = graph_.GetNode(dst.node_id).inputs[dst.port_index];
        if (!input.connections.empty())
          graph_.Disconnect(input.connections[0], dst);
        graph_.Connect(src, dst);
        Edited();
      }
    }
  }
  ed::EndCreate();
}

void PCGGraphPanel::HandleDelete() {
  if (ed::BeginDelete()) {
    ed::LinkId link;
    while (ed::QueryDeletedLink(&link)) {
      if (!ed::AcceptDeletedItem())
        continue;
      const LinkEnds ends = DecodeLinkID(link);
      graph_.Disconnect({ends.src_node, ends.src_port},
                        {ends.dst_node, ends.dst_port});
      Edited();
    }
    ed::NodeId node;
    while (ed::QueryDeletedNode(&node)) {
      if (!ed::AcceptDeletedItem())
        continue;
      graph_.RemoveNode(static_cast<uint32_t>(node.Get()));
      Edited();
    }
  }
  ed::EndDelete();
}

void PCGGraphPanel::HandleMenus() {
  ed::Suspend();
  if (ed::ShowBackgroundContextMenu()) {
    menu_ = Menu::AddNode;
    menu_at_ = ed::ScreenToCanvas(ImGui::GetMousePos());
    PopupMenu::Open();
  } else if (ed::ShowNodeContextMenu(&menu_node_)) {
    menu_ = Menu::Node;
    PopupMenu::Open();
  }
  if (PopupMenu::Begin()) {
    switch (menu_) {
      case Menu::AddNode:
        // A submenu per category, as Unreal's palette
        for (int category = 0;
             category < static_cast<int>(PCG::Category::COUNT); ++category) {
          const auto kind = static_cast<PCG::Category>(category);
          bool any = false;
          PCG::NodeTypeDB::Instance().ForEachType(
              [&](uint32_t, const PCG::NodeType& type) {
                any |= type.category == kind;
              });
          if (!any || !PopupMenu::Menu(nullptr, _CategoryName(kind)))
            continue;
          PCG::NodeTypeDB::Instance().ForEachType(
              [&](uint32_t id, const PCG::NodeType& type) {
                if (type.category == kind && PopupMenu::ItemLight(type.name))
                  AddNode(id);
              });
          PopupMenu::EndMenu();
        }
        break;
      case Menu::Node:
        if (PopupMenu::Item(ICON_FA_TRASH, "Delete Node"))
          ed::DeleteNode(menu_node_);
        break;
      case Menu::Choice:
        if (auto* node = graph_.TryGetNode(
                static_cast<uint32_t>(menu_node_.Get()))) {
          const auto& param = PCG::NodeTypeDB::Instance()
                                  .Get(node->type_id)
                                  .params[menu_param_];
          for (const std::string& item : param.enum_items)
            if (PopupMenu::ItemLight(item)) {
              node->params[menu_param_] = item;
              Edited();
            }
        }
        break;
    }
    PopupMenu::End();
  }
  PopupMenu::Pop();
  ed::Resume();
}

void PCGGraphPanel::AddNode(uint32_t type_id) {
  const PCG::NodeType& type = PCG::NodeTypeDB::Instance().Get(type_id);
  PCG::ProgramGraph::Node* node = graph_.CreateNode(type_id);
  node->inputs.resize(type.inputs.size());
  node->outputs.resize(type.outputs.size());
  node->default_inputs.resize(type.inputs.size());
  node->params.resize(type.params.size());
  for (size_t i = 0; i < type.inputs.size(); ++i)
    node->default_inputs[i] = type.inputs[i].default_value;
  for (size_t i = 0; i < type.params.size(); ++i)
    node->params[i] = type.params[i].default_value;
  node->gui_position = glm::vec2(menu_at_.x, menu_at_.y);
  ed::SetNodePosition(node->id, menu_at_);
  Edited();
}

void PCGGraphPanel::DrawClosePrompt() {
  if (close_asked_) {
    ImGui::OpenPopup("Unsaved Changes##pcg_graph");
    close_asked_ = false;
  }
  if (!ImGui::BeginPopupModal("Unsaved Changes##pcg_graph", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextUnformatted(("Save the changes to " + path_ + "?").c_str());
  if (ImGui::Button("Save")) {
    if (Save())
      Close();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Discard")) {
    dirty_ = false;
    Close();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

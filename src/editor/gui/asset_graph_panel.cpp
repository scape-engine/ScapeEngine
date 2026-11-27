#include "asset_graph_panel.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "editor/events.h"
#include "editor/gui/graph/graph_ids.h"
#include "engine/core/logger.h"
#include "engine/pcg/procmodel/descriptor/model_descriptor_parser.h"

namespace Edits = ProcModel::DescriptorEdits;

namespace {

constexpr float node_width = 260.0f;
constexpr float column_gap = 120.0f;
constexpr float row_gap = 32.0f;

// The object's array `key`, by reference; empty when it has none
const Edits::Document& _List(const Edits::Document& object, const char* key) {
  static const Edits::Document empty = Edits::Document::array();
  const auto it = object.find(key);
  return it != object.end() && it->is_array() ? *it : empty;
}

// A text field applied when left (Enter, or a click away): the new text
// then, so a half-typed id is never used
std::optional<std::string> _Text(const char* id, const std::string& value,
                                 float width) {
  static std::unordered_map<ImGuiID, std::array<char, 128>> buffers;
  const ImGuiID key = ImGui::GetID(id);
  std::array<char, 128>& buffer = buffers[key];
  if (ImGui::GetActiveID() != key)
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
  ImGui::SetNextItemWidth(width);
  ImGui::InputText(id, buffer.data(), buffer.size());
  if (!ImGui::IsItemDeactivatedAfterEdit() || value == buffer.data())
    return std::nullopt;
  return std::string(buffer.data());
}

uint32_t _Node(size_t group) {
  return static_cast<uint32_t>(group) + 1;
}

}  // namespace

AssetGraphPanel::AssetGraphPanel() {
  opened_ = EditorEvents::open_asset_graph.connect(
      [this](const std::string& descriptor) { Open(descriptor); });
}

void AssetGraphPanel::Open(const std::string& descriptor) {
  if (descriptor == path_)
    return;
  // Another model replaces this one only once its edits are dealt with
  if (dirty_) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "[AssetGraph] Save or discard " + path_ + " before opening another");
    close_asked_ = true;
    return;
  }
  std::ifstream file(Runtime::Project().AbsolutePath(descriptor));
  Document doc = Document::parse(file, nullptr, false);
  if (!file || doc.is_discarded() || !doc.is_object()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[AssetGraph] Can't read " + descriptor);
    return;
  }
  path_ = descriptor;
  doc_ = std::move(doc);
  dirty_ = close_asked_ = false;
  Edited(false, true);
}

void AssetGraphPanel::Close() {
  if (dirty_) {
    close_asked_ = true;
    return;
  }
  path_.clear();
  doc_ = Document();
  error_.clear();
}

bool AssetGraphPanel::Save() {
  if (!error_.empty())
    return false;
  std::ofstream file(Runtime::Project().AbsolutePath(path_));
  file << doc_.dump(2) << "\n";
  if (!file) {
    error_ = "Can't write " + path_;
    return false;
  }
  dirty_ = false;
  // TODO: reload the model's archetype, so previews and planets show the
  // edits without a restart
  return true;
}

void AssetGraphPanel::Edited(bool changed, bool relayout) {
  dirty_ |= changed;
  if (relayout)
    layout_in_ = 2;
  // The engine's own parser says whether the model still loads
  ProcModel::ModelDescriptor parsed;
  error_ = ProcModel::ModelDescriptorParser::FromJson(
               nlohmann::json::parse(doc_.dump()), parsed)
               ? ""
               : "The descriptor doesn't load as it is";
}

bool AssetGraphPanel::Activates(const std::string& part_id) const {
  for (const auto& group : _List(doc_, "selection_groups")) {
    const Document parent = group.value("parent", Document());
    if (parent == part_id ||
        (parent.is_array() &&
         std::find(parent.begin(), parent.end(), part_id) != parent.end()))
      return true;
  }
  return false;
}

void AssetGraphPanel::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool visible = ImGui::Begin("Asset Graph", nullptr);
  ImGui::PopStyleVar();
  if (!visible || !IsOpen()) {
    ImGui::End();
    return;
  }
  DrawToolbar();
  if (canvas_.Begin("Asset Graph")) {
    const size_t groups = _List(doc_, "selection_groups").size();
    for (size_t group = 0; group < groups; ++group)
      DrawNode(group);
    DrawLinks();
    HandleCreate();
    HandleDelete();
    HandleMenus();
    if (layout_in_ > 0 && --layout_in_ == 0) {
      Layout();
      canvas_.FrameContent();
    }
    canvas_.End();
  }
  DrawClosePrompt();
  ImGui::End();
}

void AssetGraphPanel::DrawToolbar() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  ImGui::BeginChild("##toolbar", ImVec2(0.0f, ImGui::GetFrameHeight() + 12.0f),
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar);
  const bool focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  ImGui::BeginDisabled(!dirty_ || !error_.empty());
  if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save") ||
      (focused && dirty_ &&
       ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)))
    Save();
  ImGui::EndDisabled();
  IMComponents::Tooltip("Write the descriptor (Ctrl+S)");
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_PLUS " Group")) {
    const std::string id = Edits::UniqueGroupId(doc_, "group");
    Edited(Edits::AddGroup(doc_, id, Edits::UniquePartId(doc_, id + "_A")),
           true);
  }
  IMComponents::Tooltip("A new root group with one part");
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_EXPAND " Frame"))
    canvas_.FrameContent();
  IMComponents::Tooltip("Show the whole graph");
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_SITEMAP " Arrange"))
    layout_in_ = 1;
  IMComponents::Tooltip("Lay the groups out as their tree again");

  ImGui::SameLine();
  const std::string title = path_ + (dirty_ ? " *" : "");
  ImGui::AlignTextToFramePadding();
  IMComponents::Label(title, EditorColor::text_dim);
  if (!error_.empty()) {
    ImGui::SameLine();
    IMComponents::Label(error_, EditorColor::console_error);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

void AssetGraphPanel::DrawNode(size_t group) {
  Document& node = doc_["selection_groups"][group];
  const std::string group_id = node.value("group_id", "");
  const bool root = !node.contains("parent") || node["parent"].empty();
  ImGui::PushID(static_cast<int>(group));
  GraphNode::Begin(_Node(group), node_width);

  if (auto renamed = _Text("##group", group_id, node_width))
    Edited(Edits::RenameGroup(doc_, group, *renamed));
  // A root group is placed on the model itself
  GraphNode::EndHeader(root ? EditorColor::accent_deep
                            : EditorColor::surface_4);

  GraphNode::InputPin(PinID::Encode(_Node(group), 0, true), "Activated by",
                      !root);
  bool required = node.value("required", true);
  if (IMComponents::Checkbox("Required", &required))
    Edited(Edits::SetRequired(doc_, group, required));
  if (node.contains("locators")) {
    IMComponents::Label(
        std::to_string(node["locators"].size()) + " locators" +
            (node.value("unique_per_locator", false) ? ", a part each" : ""),
        EditorColor::text_dim);
  }

  // Its parts, each a pin to the groups it activates
  const ImVec2 line = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(
      line, ImVec2(line.x + node_width, line.y), EditorColor::line_strong);
  ImGui::Dummy(ImVec2(0.0f, 4.0f));
  Document& parts = node["parts"];
  const bool last = parts.size() < 2;
  for (size_t part = 0; part < parts.size(); ++part) {
    const std::string part_id = parts[part].value("id", "");
    ImGui::PushID(static_cast<int>(part));
    float weight = parts[part].value("weight", 1.0f);
    ImGui::SetNextItemWidth(56.0f);
    ImGui::DragFloat("##weight", &weight, 0.01f, 0.0f, 100.0f, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit())
      Edited(Edits::SetWeight(doc_, group, part, weight));
    IMComponents::Tooltip("Its chance against the group's other parts");
    ImGui::SameLine();
    // TODO: pick from the model's parts (its scene's nodes), not typed
    if (auto renamed = _Text("##part", part_id, node_width - 120.0f))
      Edited(Edits::SetPart(doc_, group, part, *renamed));
    ImGui::SameLine();
    ImGui::BeginDisabled(last);
    if (ImGui::SmallButton(ICON_FA_XMARK))
      Edited(Edits::RemovePart(doc_, group, part));
    ImGui::EndDisabled();
    IMComponents::Tooltip(last ? "A group keeps one part at least"
                               : "Remove the part");
    GraphNode::OutputPin(PinID::Encode(_Node(group), part + 1, false),
                         Activates(part_id));
    ImGui::PopID();
  }
  if (ImGui::SmallButton(ICON_FA_PLUS " Part"))
    Edited(Edits::AddPart(doc_, group,
                          Edits::UniquePartId(doc_, group_id + "_part")));

  GraphNode::End();
  ImGui::PopID();
}

void AssetGraphPanel::DrawLinks() {
  // Each part's pins: a part offered by several groups activates from
  // every one of them
  std::unordered_map<std::string, std::vector<std::pair<uint32_t, uint32_t>>>
      pins;
  const Document& groups = _List(doc_, "selection_groups");
  for (size_t group = 0; group < groups.size(); ++group) {
    const Document& parts = _List(groups[group], "parts");
    for (size_t part = 0; part < parts.size(); ++part)
      pins[parts[part].value("id", "")].push_back(
          {_Node(group), static_cast<uint32_t>(part) + 1});
  }
  const auto link = [&](size_t group, const std::string& parent) {
    const auto from = pins.find(parent);
    if (from == pins.end())
      return;
    for (const auto& [node, port] : from->second)
      ed::Link(MakeLinkID(node, port, _Node(group), 0),
               PinID::Encode(node, port, false),
               PinID::Encode(_Node(group), 0, true),
               ImGui::ColorConvertU32ToFloat4(EditorColor::accent_border),
               2.0f);
  };
  for (size_t group = 0; group < groups.size(); ++group) {
    const Document parent = groups[group].value("parent", Document());
    if (parent.is_string())
      link(group, parent.get<std::string>());
    else if (parent.is_array())
      for (const auto& id : parent)
        link(group, id.get<std::string>());
  }
}

void AssetGraphPanel::HandleCreate() {
  if (ed::BeginCreate(
          ImGui::ColorConvertU32ToFloat4(EditorColor::accent_border), 2.0f)) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b)) {
      PinID from = PinID::Decode(a), to = PinID::Decode(b);
      if (from.is_input)
        std::swap(from, to);
      // From a part (output) to a group's input; to its own group too, as
      // branches recurse
      const bool valid = !from.is_input && to.is_input && from.port_index > 0;
      if (!valid) {
        ed::RejectNewItem(
            ImGui::ColorConvertU32ToFloat4(EditorColor::console_error), 2.0f);
      } else if (ed::AcceptNewItem()) {
        const std::string part_id =
            doc_["selection_groups"][from.node_id - 1]["parts"]
                [from.port_index - 1]
                    .value("id", "");
        Edited(Edits::Link(doc_, to.node_id - 1, part_id));
      }
    }
  }
  ed::EndCreate();
}

void AssetGraphPanel::HandleDelete() {
  std::vector<size_t> removed;
  if (ed::BeginDelete()) {
    ed::LinkId link;
    while (ed::QueryDeletedLink(&link)) {
      if (!ed::AcceptDeletedItem())
        continue;
      const LinkEnds ends = DecodeLinkID(link);
      const std::string part_id =
          doc_["selection_groups"][ends.src_node - 1]["parts"]
              [ends.src_port - 1]
                  .value("id", "");
      Edited(Edits::Unlink(doc_, ends.dst_node - 1, part_id));
    }
    ed::NodeId node;
    while (ed::QueryDeletedNode(&node))
      if (ed::AcceptDeletedItem())
        removed.push_back(static_cast<size_t>(node.Get()) - 1);
  }
  ed::EndDelete();
  // Last first, so the indices still hold
  std::sort(removed.rbegin(), removed.rend());
  for (const size_t group : removed)
    Edited(Edits::RemoveGroup(doc_, group), true);
}

void AssetGraphPanel::HandleMenus() {
  ed::Suspend();
  if (ed::ShowBackgroundContextMenu()) {
    menu_node_ = 0;
    menu_at_ = ed::ScreenToCanvas(ImGui::GetMousePos());
    PopupMenu::Open();
  } else if (ed::ShowNodeContextMenu(&menu_node_)) {
    PopupMenu::Open();
  }
  if (PopupMenu::Begin()) {
    if (menu_node_) {
      if (PopupMenu::Item(ICON_FA_TRASH, "Delete Group"))
        ed::DeleteNode(menu_node_);
    } else if (PopupMenu::Item(ICON_FA_PLUS, "Add Group")) {
      const std::string id = Edits::UniqueGroupId(doc_, "group");
      if (Edits::AddGroup(doc_, id, Edits::UniquePartId(doc_, id + "_A"))) {
        const size_t groups = doc_["selection_groups"].size();
        ed::SetNodePosition(_Node(groups - 1), menu_at_);
        Edited(true);
      }
    }
    PopupMenu::End();
  }
  PopupMenu::Pop();
  ed::Resume();
}

void AssetGraphPanel::DrawClosePrompt() {
  if (close_asked_) {
    ImGui::OpenPopup("Unsaved Changes##asset_graph");
    close_asked_ = false;
  }
  if (!ImGui::BeginPopupModal("Unsaved Changes##asset_graph", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextUnformatted(("Save the changes to " + path_ + "?").c_str());
  if (!error_.empty())
    IMComponents::Label(error_, EditorColor::console_error);
  ImGui::BeginDisabled(!error_.empty());
  if (ImGui::Button("Save")) {
    if (Save())
      Close();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
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

void AssetGraphPanel::Layout() {
  const Document& groups = _List(doc_, "selection_groups");
  std::unordered_map<std::string, size_t> part_group;
  for (size_t group = 0; group < groups.size(); ++group)
    for (const auto& part : _List(groups[group], "parts"))
      part_group[part.value("id", "")] = group;

  // Each group under its first parent's group; the others are roots
  std::vector<std::vector<size_t>> children(groups.size());
  std::vector<size_t> roots;
  for (size_t group = 0; group < groups.size(); ++group) {
    const Document parent = groups[group].value("parent", Document());
    const std::string first =
        parent.is_string()                       ? parent.get<std::string>()
        : parent.is_array() && !parent.empty() ? parent[0].get<std::string>()
                                                 : "";
    const auto found = part_group.find(first);
    if (found == part_group.end() || found->second == group)
      roots.push_back(group);
    else
      children[found->second].push_back(group);
  }

  const auto height = [](size_t group) {
    return ed::GetNodeSize(_Node(group)).y;
  };
  // A subtree's height; each group once, so a cycle can't recurse forever
  std::vector<float> span(groups.size(), -1.0f);
  std::function<float(size_t)> measure = [&](size_t group) {
    if (span[group] >= 0.0f)
      return span[group];
    span[group] = height(group);
    float below = 0.0f;
    for (const size_t child : children[group])
      below += measure(child) + row_gap;
    span[group] = std::max(height(group), below - row_gap);
    return span[group];
  };
  std::vector<bool> placed(groups.size(), false);
  std::function<void(size_t, int, float)> place = [&](size_t group, int depth,
                                                      float top) {
    if (placed[group])
      return;
    placed[group] = true;
    ed::SetNodePosition(
        _Node(group),
        ImVec2(depth * (node_width + column_gap),
               top + (span[group] - height(group)) * 0.5f));
    for (const size_t child : children[group]) {
      place(child, depth + 1, top);
      top += span[child] + row_gap;
    }
  };
  float top = 0.0f;
  for (const size_t root : roots) {
    measure(root);
    place(root, 0, top);
    top += span[root] + row_gap * 2.0f;
  }
}

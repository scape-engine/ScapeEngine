#include "asset_graph_editor.h"
#include <ImGuiFileDialog/ImGuiFileDialog.h>

#include <cmath>       // Needed for std::abs, std::round
#include <filesystem>  // Needed to extract filenames for new JSONs
#include <functional>
#include <unordered_set>  // Needed for cycle protection

#include "engine/content/io/json.h"
#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/pcg/pcg_engine.h"

#include <cctype>
#include <iostream>
#include <map>

// TODO:
// - Separate JSON serialization structs into dedicated file
// - Split RenderNodeGraph into smaller functions
// - Move graph state out of editor class

// Fallback definitions for icons if they are not provided by the engine's
// global icon header. Each macro has its own block to prevent compilation
// errors if some icons are partially defined.
#ifndef ICON_FA_PLUS
#define ICON_FA_PLUS "+"
#endif
#ifndef ICON_FA_FLOPPY_DISK
#define ICON_FA_FLOPPY_DISK "Save"
#endif
#ifndef ICON_FA_FOLDER_OPEN
#define ICON_FA_FOLDER_OPEN "Load"
#endif
#ifndef ICON_FA_LINK
#define ICON_FA_LINK "@"
#endif
#ifndef ICON_FA_EXCLAMATION_TRIANGLE
#define ICON_FA_EXCLAMATION_TRIANGLE "!"
#endif
#ifndef ICON_FA_SLIDERS
#define ICON_FA_SLIDERS "Sliders"
#endif
#ifndef ICON_FA_FILE
#define ICON_FA_FILE "New"
#endif
#ifndef ICON_FA_WRENCH
#define ICON_FA_WRENCH "W"
#endif

namespace ed = ax::NodeEditor;

// Dialogue states for modal alerts
static bool s_show_not_implemented_popup = false;
static bool s_show_error_popup = false;
static std::string s_error_message = "";

// Raw JSON cache to preserve extra data (like deformations and pipeline)
// that isn't strictly defined in the ModelDescriptor C++ struct.
static nlohmann::json s_RawJsonCache;

// --- JSON serialization definitions ---

namespace glm {
void to_json(nlohmann::json& j, const vec3& v) {
  j = nlohmann::json::array({v.x, v.y, v.z});
}
}  // namespace glm

NLOHMANN_JSON_SERIALIZE_ENUM(
    ProcModel::ConstraintRule::Type,
    {{ProcModel::ConstraintRule::Type::EXCLUDES, "excludes"},
     {ProcModel::ConstraintRule::Type::REQUIRES, "requires"}})

namespace ProcModel {

void to_json(nlohmann::json& j, const PartDescriptor& p) {
  j = nlohmann::json{{"id", p.id}, {"name", p.name}, {"weight", p.weight}};
}
void to_json(nlohmann::json& j, const ConstraintRule& c) {
  j = nlohmann::json{{"id", c.id},
                     {"type", c.type},
                     {"part_a", c.part_a},
                     {"part_b", c.part_b}};
}
void to_json(nlohmann::json& j, const ParameterBinding& p) {
  j = nlohmann::json{{"source_part", p.source_part},
                     {"source_param", p.source_param},
                     {"target_part", p.target_part},
                     {"target_param", p.target_param},
                     {"ratio", p.ratio}};
}
void to_json(nlohmann::json& j, const SelectionGroup& g) {
  j = nlohmann::json{{"group_id", g.group_id},
                     {"required", g.required},
                     {"parent", g.parent},
                     {"parts", g.parts}};
  if (g.locators) {
    j["locators"] = *g.locators;
  }
  if (g.unique_per_locator) {
    j["unique_per_locator"] = g.unique_per_locator;
  }
}

// Serialization of TransformRange
void to_json(nlohmann::json& j, const TransformRange& p) {
  j = nlohmann::json{{"part_id", p.part_id}};
  if (p.scale_min)
    j["scale_min"] = *p.scale_min;
  if (p.scale_max)
    j["scale_max"] = *p.scale_max;
  if (p.rotation_min)
    j["rotation_min"] = *p.rotation_min;
  if (p.rotation_max)
    j["rotation_max"] = *p.rotation_max;
}

void to_json(nlohmann::json& j, const LocatorOperation& sm) {
  j = nlohmann::json{{"kind", sm.entry.kind}, {"params", sm.entry.params}};
  if (!sm.target_group_id.empty())
    j["target_group_id"] = sm.target_group_id;
}

void to_json(nlohmann::json& j, const ModelDescriptor& m) {
  j = nlohmann::json{{"model_id", m.model_id},
                     {"model_name", m.model_name},
                     {"path", m.path},
                     {"domain", m.domain},
                     {"selection_groups", m.selection_groups},
                     {"transform_ranges", m.transform_ranges},
                     {"constraints", m.constraints},
                     {"parameter_bindings", m.parameter_bindings}};
  if (!m.locator_operations.empty())
    j["locator_operations"] = m.locator_operations;
  if (m.scale_min)
    j["scale_min"] = *m.scale_min;
  if (m.scale_max)
    j["scale_max"] = *m.scale_max;
  if (!m.tags.empty())
    j["tags"] = m.tags;
}

}  // namespace ProcModel

// --- Utility Helpers ---

// Converts strings to unique numerical IDs for ImGui context tracking
static uintptr_t HashString(const std::string& str) {
  return std::hash<std::string>{}(str);
}

// Collapses lists of locators into a clean counted label in the UI
static std::map<std::string, int> GroupLocatorPoints(
    const std::vector<std::string>& locators) {
  std::map<std::string, int> grouped;
  for (const auto& locator : locators) {
    std::string base_name = locator;
    while (!base_name.empty() && std::isdigit(base_name.back()))
      base_name.pop_back();
    while (!base_name.empty() &&
           (base_name.back() == '_' || base_name.back() == '.'))
      base_name.pop_back();
    grouped[base_name]++;
  }
  return grouped;
}

// Extracts the parent category from a node's group ID
static std::string ExtractCategoryName(const std::string& group_id) {
  std::string category = group_id;

  while (!category.empty() && category.front() == '_') {
    category.erase(0, 1);
  }

  size_t pos = category.find('_');
  if (pos != std::string::npos) {
    category = category.substr(0, pos);
  }

  return category;
}

// Generates a consistent, dark-pastel color for the node header
static ImU32 GenerateGroupHeaderColor(const std::string& group_id) {
  std::string category = ExtractCategoryName(group_id);

  if (category == "BASE" || category == "TRUNK") {
    return IM_COL32(70, 70, 70, 255);
  }

  size_t hash = std::hash<std::string>{}(category);
  int r = (hash & 0xFF0000) >> 16;
  int g = (hash & 0x00FF00) >> 8;
  int b = (hash & 0x0000FF);

  r = (r + 40) / 2;
  g = (g + 40) / 2;
  b = (b + 80) / 2;

  return IM_COL32(r, g, b, 255);
}

// Retrieves or initializes deformation data for a given group ID from the cache
static nlohmann::json& GetDeformationData(const std::string& group_id) {
  if (!s_RawJsonCache.contains("part_deformation_ranges")) {
    s_RawJsonCache["part_deformation_ranges"] = nlohmann::json::array();
  }
  for (auto& def : s_RawJsonCache["part_deformation_ranges"]) {
    if (def.value("group_id", "") == group_id) {
      return def;
    }
  }
  s_RawJsonCache["part_deformation_ranges"].push_back({{"group_id", group_id}});
  return s_RawJsonCache["part_deformation_ranges"].back();
}

// --- Editor Implementation ---

AssetGraphEditor::AssetGraphEditor(PreviewData* data) : data_(data) {
  ed::Config config;
  config.SettingsFile = "AssetGraphEditor.json";
  m_EditorContext = ed::CreateEditor(&config);

  ed::SetCurrentEditor(m_EditorContext);
  ed::Style& style = ed::GetStyle();
  auto C = [](ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); };
  style.Colors[ed::StyleColor_Bg]            = C(EditorColor::void_bg);
  style.Colors[ed::StyleColor_Grid]          = C(EditorColor::grid_minor);
  style.Colors[ed::StyleColor_NodeBg]        = C(EditorColor::panel);
  style.Colors[ed::StyleColor_NodeBorder]    = C(EditorColor::control_border);
  style.Colors[ed::StyleColor_HovNodeBorder] = C(EditorColor::accent_hover);
  style.Colors[ed::StyleColor_SelNodeBorder] = C(EditorColor::accent);
  style.Colors[ed::StyleColor_SelLinkBorder] = C(EditorColor::accent);
  style.Colors[ed::StyleColor_PinRect]       = C(EditorColor::accent_border);
  style.NodeRounding = EditorSizes::control_radius;
  style.NodeBorderWidth = 1.0f;
  ed::SetCurrentEditor(nullptr);
}

AssetGraphEditor::~AssetGraphEditor() {
  ed::DestroyEditor(m_EditorContext);
}

void AssetGraphEditor::LoadGraph(const std::string& filepath) {
  m_ModelData = ProcModel::ModelDescriptor{};

  auto json_opt = Content::ReadJsonFile(filepath);
  if (!json_opt.has_value()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[AssetGraphEditor] Failed to read: " + filepath);
    return;
  }

  // Store the raw JSON to preserve deformation settings and unknown schemas
  // safely
  s_RawJsonCache = json_opt.value();

  if (ProcModel::ModelDescriptorParser::FromJson(s_RawJsonCache, m_ModelData)) {
    m_CurrentFilePath = filepath;
    m_NeedsAutoLayout = true;
    Logger::getInstance().Log(LogLevel::Info,
                              "[AssetGraphEditor] Loaded: " + filepath);
  } else {
    Logger::getInstance().Log(
        LogLevel::Error, "[AssetGraphEditor] Failed to parse: " + filepath);
  }
}

void AssetGraphEditor::SaveGraph(const std::string& filepath) {
  if (filepath.empty())
    return;

  // Convert the known struct data to JSON
  nlohmann::json j = m_ModelData;

  // Merge the known struct data back into the raw cache
  // This updates topology but preserves 'pipeline' and
  // 'part_deformation_ranges'
  for (auto& el : j.items()) {
    s_RawJsonCache[el.key()] = el.value();
  }

  if (!Content::WriteJsonFile(filepath, s_RawJsonCache, 2)) {
    s_error_message = "Failed to write to file:\n" + filepath;
    s_show_error_popup = true;
    Logger::getInstance().Log(LogLevel::Error,
                              "[AssetGraphEditor] Failed to save: " + filepath);
    return;
  }

  Logger::getInstance().Log(LogLevel::Info,
                            "[AssetGraphEditor] Saved: " + filepath);

  if (data_) {
    auto& pcg = EngineContext::PCG().GetProcModel();
    data_->archetype_id = pcg.LoadArchetype(filepath);
    data_->instances.clear();
  }
}

void AssetGraphEditor::Render() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("Asset Graph", nullptr);

  // Toolbar area
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
  ImGui::BeginChild("GraphToolbar", ImVec2(0, 36.0f), true,
                    ImGuiWindowFlags_NoScrollbar);

  if (ImGui::Button(ICON_FA_FILE " New")) {
    m_ModelData = ProcModel::ModelDescriptor{};
    m_ModelData.model_id = "new_model";
    m_ModelData.model_name = "New Model";

    s_RawJsonCache = nlohmann::json::object();

    ProcModel::SelectionGroup root_group;
    root_group.group_id = "_BASE_";
    root_group.required = true;

    ProcModel::PartDescriptor default_part;
    default_part.id = "BASE_A";
    default_part.name = "Default Style";
    default_part.weight = 1.0f;

    root_group.parts.push_back(default_part);
    m_ModelData.selection_groups.push_back(root_group);

    m_CurrentFilePath = "";
    m_NeedsAutoLayout = true;
  }
  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load")) {
    IGFD::FileDialogConfig cfg{};
    cfg.path = std::filesystem::current_path().string();
    IGFD::FileDialog::Instance()->OpenDialog("GraphLoadDlg",
                                             "Select Descriptor", ".json", cfg);
  }

  if (IGFD::FileDialog::Instance()->Display("GraphLoadDlg",
                                            ImGuiWindowFlags_NoCollapse,
                                            ImVec2(700.0f, 500.0f))) {
    if (IGFD::FileDialog::Instance()->IsOk()) {
      LoadGraph(IGFD::FileDialog::Instance()->GetFilePathName());
    }
    IGFD::FileDialog::Instance()->Close();
  }

  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save")) {
    if (m_CurrentFilePath.empty()) {
      IGFD::FileDialogConfig cfg{};
      cfg.path = std::filesystem::current_path().string();
      IGFD::FileDialog::Instance()->OpenDialog(
          "GraphSaveAsDlg", "Save Descriptor As", ".json", cfg);
    } else {
      SaveGraph(m_CurrentFilePath);
    }
  }

  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save As")) {
    IGFD::FileDialogConfig cfg{};
    cfg.path = std::filesystem::current_path().string();
    IGFD::FileDialog::Instance()->OpenDialog(
        "GraphSaveAsDlg", "Save Descriptor As", ".json", cfg);
  }

  if (IGFD::FileDialog::Instance()->Display("GraphSaveAsDlg",
                                            ImGuiWindowFlags_NoCollapse,
                                            ImVec2(700.0f, 500.0f))) {
    if (IGFD::FileDialog::Instance()->IsOk()) {
      m_CurrentFilePath = IGFD::FileDialog::Instance()->GetFilePathName();

      std::string filename = std::filesystem::path(m_CurrentFilePath)
                                 .make_preferred()
                                 .stem()
                                 .string();
      if (m_ModelData.model_id == "new_model" || m_ModelData.model_id.empty()) {
        m_ModelData.model_id = filename;
        m_ModelData.model_name = filename;
      }

      SaveGraph(m_CurrentFilePath);
    }
    IGFD::FileDialog::Instance()->Close();
  }

  if (IGFD::FileDialog::Instance()->Display("ModelPathDlg",
                                            ImGuiWindowFlags_NoCollapse,
                                            ImVec2(700.0f, 500.0f))) {
    if (IGFD::FileDialog::Instance()->IsOk()) {
      m_ModelData.path = IGFD::FileDialog::Instance()->GetFilePathName();
    }
    IGFD::FileDialog::Instance()->Close();
  }

  ImGui::SameLine();
  ImGui::TextDisabled(" | ");
  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_PLUS " Add Node")) {
    ProcModel::SelectionGroup new_group;
    new_group.group_id =
        "NEW_GROUP_" + std::to_string(m_ModelData.selection_groups.size());
    m_ModelData.selection_groups.push_back(new_group);
  }

  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_SLIDERS " Constraints")) {
    s_show_not_implemented_popup = true;
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_SLIDERS " Bindings")) {
    s_show_not_implemented_popup = true;
  }

  ImGui::SameLine();
  std::string display_name =
      m_CurrentFilePath.empty() ? "Unsaved File" : m_ModelData.model_name;
  ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  Editing: %s",
                     display_name.c_str());

  ImGui::EndChild();
  ImGui::PopStyleVar();

  // Safety dialogue modals
  if (s_show_not_implemented_popup) {
    ImGui::OpenPopup("Feature Under Development");
    s_show_not_implemented_popup = false;
  }
  if (ImGui::BeginPopupModal("Feature Under Development", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("This feature is currently under active development.");
    ImGui::Text("Please check back in a future engine update.");
    ImGui::Dummy(ImVec2(0, 10));
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (s_show_error_popup) {
    ImGui::OpenPopup("Error Alert");
    s_show_error_popup = false;
  }
  if (ImGui::BeginPopupModal("Error Alert", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                       "%s Error Detected:", ICON_FA_EXCLAMATION_TRIANGLE);
    ImGui::Text("%s", s_error_message.c_str());
    ImGui::Dummy(ImVec2(0, 10));
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  RenderNodeGraph();

  ImGui::End();
  ImGui::PopStyleVar();
}

// --- Layout algorithm ---
// Automatically organizes nodes into a clean horizontal tree structure
void AssetGraphEditor::AutoLayoutNodes() {
  if (m_ModelData.selection_groups.empty())
    return;

  // Map parts to their parent group index to avoid identical group_id conflicts
  std::unordered_map<std::string, int> partToGroupIdx;
  for (int i = 0; i < m_ModelData.selection_groups.size(); ++i) {
    for (const auto& part : m_ModelData.selection_groups[i].parts) {
      partToGroupIdx[part.id] = i;
    }
  }

  std::unordered_map<int, std::vector<int>> tree;
  std::vector<int> roots;

  // Build the hierarchical tree based on 'parent' links
  for (int i = 0; i < m_ModelData.selection_groups.size(); ++i) {
    auto& group = m_ModelData.selection_groups[i];
    if (group.parent.empty()) {
      roots.push_back(i);
    } else {
      // Attach to the first parent's group for layout purposes.
      // Multi-parent groups will visually appear under one branch;
      // the other connections still render as links.
      const std::string& first_parent_part = group.parent.front();
      if (partToGroupIdx.count(first_parent_part) == 0) {
        roots.push_back(i);
      } else {
        tree[partToGroupIdx[first_parent_part]].push_back(i);
      }
    }
  }

  std::unordered_map<int, float> nodeHeights;
  std::unordered_map<int, float> subtreeHeights;
  std::unordered_set<int> visited;

  // First pass: compute the total height required by each branch to avoid
  // overlapping
  std::function<float(int)> computeHeight = [&](int node_idx) {
    if (visited.count(node_idx))
      return 0.0f;
    visited.insert(node_idx);

    auto& node = m_ModelData.selection_groups[node_idx];
    float h = 100.0f + (node.parts.size() * 32.0f);

    bool has_explicit_locators =
        node.locators.has_value() && !node.locators->empty();
    if (has_explicit_locators)
      h += 35.0f;

    // Account for locator modifiers section header height
    bool has_locator_operations =
        std::any_of(m_ModelData.locator_operations.begin(),
                    m_ModelData.locator_operations.end(),
                    [&](const ProcModel::LocatorOperation& sm) {
                      return sm.target_group_id.empty() ||
                             sm.target_group_id == node.group_id;
                    });
    if (has_locator_operations)
      h += 30.0f;

    // Dynamically calculate height if deformation settings exist for this group
    bool has_deformations = false;
    if (s_RawJsonCache.contains("part_deformation_ranges")) {
      for (auto& def : s_RawJsonCache["part_deformation_ranges"]) {
        if (def.value("group_id", "") == node.group_id) {
          has_deformations = true;
          // Add height for each numeric slider dynamically mapped
          for (auto& el : def.items()) {
            if (el.value().is_number())
              h += 30.0f;
          }
          break;
        }
      }
    }
    if (has_deformations)
      h += 40.0f;  // Padding for the folder toggle

    nodeHeights[node_idx] = h;

    float childrenH = 0.0f;
    if (tree.count(node_idx)) {
      for (int child_idx : tree[node_idx]) {
        childrenH += computeHeight(child_idx) + 40.0f;
      }
      if (childrenH > 0)
        childrenH -= 40.0f;
    }
    subtreeHeights[node_idx] = std::max(h, childrenH);
    return subtreeHeights[node_idx];
  };

  for (int root : roots)
    computeHeight(root);

  // Second pass: physically assign positions based on the heights calculated
  visited.clear();
  std::function<void(int, int, float)> placeNode = [&](int node_idx, int depth,
                                                       float startY) {
    if (visited.count(node_idx))
      return;
    visited.insert(node_idx);

    auto& node = m_ModelData.selection_groups[node_idx];
    // Create highly unique ID combining group ID and array index
    ed::NodeId id = HashString(node.group_id + "_" + std::to_string(node_idx));

    float x = depth * 420.0f;
    float y =
        startY + (subtreeHeights[node_idx] - nodeHeights[node_idx]) * 0.5f;
    ed::SetNodePosition(id, ImVec2(x, y));

    if (tree.count(node_idx)) {
      float childY = startY;
      for (int child_idx : tree[node_idx]) {
        placeNode(child_idx, depth + 1, childY);
        childY += subtreeHeights[child_idx] + 40.0f;
      }
    }
  };

  float currentRootY = 0.0f;
  for (int root : roots) {
    placeNode(root, 0, currentRootY);
    currentRootY += subtreeHeights[root] + 80.0f;
  }
}

// --- Node Graph Loop ---
void AssetGraphEditor::RenderNodeGraph() {
  // Capture the editor's screen space coordinates before starting it
  ImVec2 editor_pos = ImGui::GetCursorScreenPos();
  ImVec2 editor_size = ImGui::GetContentRegionAvail();

  // Skip rendering if canvas is not yet valid
  if (editor_size.x < 10.0f || editor_size.y < 10.0f)
    return;

  ed::SetCurrentEditor(m_EditorContext);
  ed::Begin("PCG_Node_Editor");

  bool trigger_nav = false;

  if (m_NeedsAutoLayout) {
    AutoLayoutNodes();
    trigger_nav = true;
    m_NeedsAutoLayout = false;
  }

  m_PinIdToString.clear();
  m_PinIdToGroup.clear();
  std::unordered_map<uintptr_t, int> m_InputPinToGroupIdx;

  const float nodeWidth = 300.0f;

  int group_index = 0;
  for (auto& group : m_ModelData.selection_groups) {
    // Tighten node padding horizontally so pins can sit nicely on the outer
    // edges
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(4.0f, 8.0f, 4.0f, 8.0f));

    // Guarantee unique IDs for identical group_ids
    ed::NodeId nodeId =
        HashString(group.group_id + "_" + std::to_string(group_index));
    ed::BeginNode(nodeId);

    // Capture the exact top-left coordinate of the node's content area
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // Calculate header dimensions safely
    float headerHeight = ImGui::GetTextLineHeight() + 14.0f;
    ImVec2 headerMin = ImVec2(cursorPos.x - 4.0f, cursorPos.y - 8.0f);
    ImVec2 headerMax =
        ImVec2(headerMin.x + nodeWidth + 8.0f, headerMin.y + headerHeight);

    // Draw the colored header background
    ImGui::GetWindowDrawList()->AddRectFilled(
        headerMin, headerMax, GenerateGroupHeaderColor(group.group_id),
        ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);

    // Position the text perfectly inside the drawn header box (fixes leaking)
    ImGui::SetCursorScreenPos(ImVec2(headerMin.x + 8.0f, headerMin.y + 6.0f));

    // Prevent the _BASE_ node from being renamed, as it is the mandatory root.
    if (group.group_id == "_BASE_") {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
      ImGui::TextUnformatted(group.group_id.c_str());
      ImGui::PopStyleColor();
    } else {
      // Editable Group ID for all other nodes
      char group_id_buf[128] = {0};
      strncpy(group_id_buf, group.group_id.c_str(), sizeof(group_id_buf) - 1);

      ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 50));
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
      ImGui::SetNextItemWidth(nodeWidth - 60.0f);

      if (ImGui::InputText(("##groupid_" + std::to_string(group_index)).c_str(),
                           group_id_buf, sizeof(group_id_buf))) {
        std::string new_id = group_id_buf;
        if (new_id != group.group_id && !new_id.empty()) {
          // Transfer current node position to the renamed hash-id
          ed::NodeId old_id =
              HashString(group.group_id + "_" + std::to_string(group_index));
          ed::NodeId new_id_hash =
              HashString(new_id + "_" + std::to_string(group_index));
          ed::SetNodePosition(new_id_hash, ed::GetNodePosition(old_id));
          group.group_id = new_id;
        }
      }
      ImGui::PopStyleColor(2);
    }

    if (group.required) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Req)");
    }

    // Push the cursor down beneath the header so the rest of the node content
    // renders correctly
    ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, headerMax.y + 8.0f));

    // Force the node to expand to our desired fixed width
    ImGui::Dummy(ImVec2(nodeWidth, 0.0f));

    // Only draw the "Activate" input pin for non-root nodes
    if (group.group_id != "_BASE_") {
      ed::PinId inputPinId =
          HashString(group.group_id + "_IN_" + std::to_string(group_index));
      m_InputPinToGroupIdx[inputPinId.Get()] = group_index;

      ed::BeginPin(inputPinId, ed::PinKind::Input);
      ImVec2 posIn = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(12, 12));
      ImGui::GetWindowDrawList()->AddCircleFilled(
          ImVec2(posIn.x + 6, posIn.y + 6), 5.0f, IM_COL32(220, 180, 50, 255));
      ImGui::GetWindowDrawList()->AddCircle(ImVec2(posIn.x + 6, posIn.y + 6),
                                            5.0f, IM_COL32(30, 30, 30, 255), 12,
                                            1.5f);
      ed::EndPin();

      ImGui::SameLine();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
      ImGui::Text("Activate");
    } else {
      // Label specifically for the root base node
      ImGui::Dummy(ImVec2(0, 4));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Root Node");

      // Let the user define the global model .glb path via a text field and
      // file picker button
      ImGui::Dummy(ImVec2(0, 8));

      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
      ImGui::Text("Scene File Path (.glb, .gltf):");
      ImGui::PopStyleColor();

      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12.0f);

      // Calculate widths for the input text and the folder button
      float folderBtnWidth = 28.0f;
      ImGui::SetNextItemWidth(nodeWidth - 24.0f - folderBtnWidth - 4.0f);

      char path_buf[512] = {0};
      strncpy(path_buf, m_ModelData.path.c_str(), sizeof(path_buf) - 1);

      ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 20, 255));
      if (ImGui::InputText(
              ("##model_path_" + std::to_string(group_index)).c_str(), path_buf,
              sizeof(path_buf))) {
        m_ModelData.path = path_buf;
      }
      ImGui::PopStyleColor();

      // Add File Picker Button right next to the text input
      ImGui::SameLine();
      if (ImGui::Button(
              (ICON_FA_FOLDER_OPEN "##pick_model" + std::to_string(group_index))
                  .c_str(),
              ImVec2(folderBtnWidth, 0))) {
        IGFD::FileDialogConfig cfg{};
        cfg.path = std::filesystem::current_path().string();
        // Open a file dialog restricted to 3D model formats
        IGFD::FileDialog::Instance()->OpenDialog(
            "ModelPathDlg", "Select Scene File", ".glb,.gltf", cfg);
      }

      ImGui::Dummy(ImVec2(0, 8));
    }

    // Display locator summaries if any part declares explicit locators
    std::vector<std::string> all_locators;
    if (group.locators)
      all_locators = *group.locators;

    if (!all_locators.empty()) {
      ImGui::Dummy(ImVec2(0, 4));
      auto grouped_locators = GroupLocatorPoints(all_locators);

      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
      ImGui::Text("Locators:");
      for (const auto& [base_name, count] : grouped_locators) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::Text(" %s %s (%d)", ICON_FA_LINK, base_name.c_str(), count);
      }
      ImGui::PopStyleColor();
      ImGui::Dummy(ImVec2(0, 4));
    }

    {
      ImGui::Dummy(ImVec2(0, 4));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);

      ImGuiID mod_id =
          ImGui::GetID(("mods_" + std::to_string(group_index)).c_str());
      // ? NOTE: locator_operations are on descriptor, not group, needs to
      // filter by group_id Collect modifiers targeting this group
      std::vector<int> matching_mod_indices;
      for (int mi = 0; mi < (int)m_ModelData.locator_operations.size(); ++mi) {
        const auto& sm = m_ModelData.locator_operations[mi];
        if (sm.target_group_id.empty() || sm.target_group_id == group.group_id)
          matching_mod_indices.push_back(mi);
      }

      bool mods_open = ImGui::GetStateStorage()->GetInt(mod_id, 0);
      ImVec2 start_pos = ImGui::GetCursorScreenPos();
      ImVec2 btn_size =
          ImVec2(nodeWidth - 16.0f, ImGui::GetTextLineHeight() + 8.0f);

      if (ImGui::InvisibleButton(
              ("##mod_toggle_" + std::to_string(group_index)).c_str(),
              btn_size)) {
        mods_open = !mods_open;
        ImGui::GetStateStorage()->SetInt(mod_id, mods_open);
      }

      // Display locator modifier
      ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 8.0f, start_pos.y + 4.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.9f, 0.7f, 1.0f));
      ImGui::TextUnformatted(((mods_open ? "- " : "+ ") +
                              std::string("Locator Modifiers (") +
                              std::to_string(matching_mod_indices.size()) + ")")
                                 .c_str());
      ImGui::PopStyleColor();
      ImGui::SetCursorScreenPos(
          ImVec2(start_pos.x - 8.0f, start_pos.y + btn_size.y + 4.0f));

      if (mods_open) {
        for (int mi : matching_mod_indices) {
          auto& sm = m_ModelData.locator_operations[mi];
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);
          ImGui::TextDisabled("%s", sm.entry.kind.c_str());
        }
        ImGui::Dummy(ImVec2(0, 4));
      }
    }

    // --- Dynamic deformation settings toggle ---
    nlohmann::json* def_data_ptr = nullptr;
    if (s_RawJsonCache.contains("part_deformation_ranges")) {
      for (auto& def : s_RawJsonCache["part_deformation_ranges"]) {
        if (def.value("group_id", "") == group.group_id) {
          def_data_ptr = &def;
          break;
        }
      }
    }

    // Only render the deformation settings if they explicitly exist for this
    // group
    if (def_data_ptr != nullptr) {
      ImGui::Dummy(ImVec2(0, 4));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);

      // Fetch open state safely using ImGui storage
      ImGuiID deform_id =
          ImGui::GetID(("deform_" + std::to_string(group_index)).c_str());
      bool deform_open = ImGui::GetStateStorage()->GetInt(deform_id, 0);

      ImVec2 start_pos = ImGui::GetCursorScreenPos();
      ImVec2 btn_size =
          ImVec2(nodeWidth - 16.0f, ImGui::GetTextLineHeight() + 8.0f);

      // Invisible button replaces TreeNode to fix the infinite width highlight
      // bug inside NodeEditor
      bool clicked = ImGui::InvisibleButton(
          ("##def_toggle_" + std::to_string(group_index)).c_str(), btn_size);
      bool hovered = ImGui::IsItemHovered();

      if (clicked) {
        deform_open = !deform_open;
        ImGui::GetStateStorage()->SetInt(deform_id, deform_open);
      }

      // Draw subtle background highlight if hovered
      if (hovered) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            start_pos,
            ImVec2(start_pos.x + btn_size.x, start_pos.y + btn_size.y),
            IM_COL32(255, 255, 255, 25), 4.0f);
      }

      // Draw text on top of the invisible button area
      ImGui::SetCursorScreenPos(ImVec2(start_pos.x + 8.0f, start_pos.y + 4.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 0.9f, 1.0f));
      std::string deform_label =
          (deform_open ? "- " : "+ ") + std::string("Deformation Settings");
      ImGui::TextUnformatted(deform_label.c_str());
      ImGui::PopStyleColor();

      // Move cursor below the button for the next elements
      ImGui::SetCursorScreenPos(
          ImVec2(start_pos.x - 8.0f, start_pos.y + btn_size.y + 4.0f));

      if (deform_open) {
        // Dynamically iterate over all keys in the deformation json object
        // This prevents hardcoding schemas and scales automatically
        for (auto& el : def_data_ptr->items()) {
          if (el.key() == "group_id")
            continue;  // Skip the identifier string

          if (el.value().is_number()) {
            float val = el.value().get<float>();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 16.0f);  // Extra indent for children
            ImGui::TextDisabled("%s", el.key().c_str());

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);
            ImGui::SetNextItemWidth(nodeWidth -
                                    48.0f);  // Adjust width for indent

            // Unique slider ID mapping
            if (ImGui::DragFloat(
                    ("##" + el.key() + std::to_string(group_index)).c_str(),
                    &val, 0.01f)) {
              el.value() = val;  // Write the float back to the json object
            }
          }
        }
        ImGui::Dummy(ImVec2(0, 4));
      }
    }

    // Draw a horizontal line separating the settings from the output styles
    ImGui::Dummy(ImVec2(0, 4));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p0.x, p0.y),
                                        ImVec2(p0.x + nodeWidth, p0.y),
                                        IM_COL32(80, 80, 80, 255), 1.0f);
    ImGui::Dummy(ImVec2(0, 4));

    // Render individual parts as output pins
    int part_index = 0;
    for (auto& part : group.parts) {
      ed::PinId outputPinId = HashString(part.id);
      m_PinIdToString[outputPinId.Get()] = part.id;

      ImGui::PushID(part.id.c_str());

      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
      ImGui::PushItemWidth(70.0f);
      ImGui::SliderFloat("##w", &part.weight, 0.0f, 5.0f, "W: %.1f");
      ImGui::PopItemWidth();

      ImGui::SameLine();

      // Editable Part ID
      char part_id_buf[128] = {0};
      strncpy(part_id_buf, part.id.c_str(), sizeof(part_id_buf) - 1);

      // Strict constraint on text box width to guarantee horizontal clearance
      // for the output pin
      ImGui::SetNextItemWidth(nodeWidth - 130.0f);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 20, 255));

      std::string part_ui_id = "##partid_" + std::to_string(group_index) + "_" +
                               std::to_string(part_index);
      if (ImGui::InputText(part_ui_id.c_str(), part_id_buf,
                           sizeof(part_id_buf))) {
        std::string new_part_id = part_id_buf;
        if (new_part_id != part.id && !new_part_id.empty()) {
          // Safely update all graph connections pointing to the old name
          for (auto& g : m_ModelData.selection_groups) {
            for (auto& parent_id : g.parent) {
              if (parent_id == part.id)
                parent_id = new_part_id;
            }
          }
          part.id = new_part_id;
          part.name = new_part_id;
        }
      }
      ImGui::PopStyleColor();

      // Align the output pin exactly to the far right margin
      ImGui::SameLine(nodeWidth - 16.0f);
      ed::BeginPin(outputPinId, ed::PinKind::Output);
      ImVec2 posOut = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(12, 12));
      ImGui::GetWindowDrawList()->AddCircleFilled(
          ImVec2(posOut.x + 6, posOut.y + 6), 5.0f,
          IM_COL32(100, 180, 220, 255));
      ImGui::GetWindowDrawList()->AddCircle(ImVec2(posOut.x + 6, posOut.y + 6),
                                            5.0f, IM_COL32(30, 30, 30, 255), 12,
                                            1.5f);
      ed::EndPin();

      ImGui::PopID();
      part_index++;
    }

    // Button to append new parts to this group
    ImGui::Dummy(ImVec2(0, 5));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);

    // Using group_index guarantees the ImGui context remains unique across
    // groups with identical IDs
    ImGui::PushID(group_index);
    if (ImGui::Button("+ Add Part", ImVec2(nodeWidth - 16.0f, 0))) {
      ProcModel::PartDescriptor new_part;
      new_part.id =
          group.group_id + "_NEW_PART_" + std::to_string(group.parts.size());
      new_part.name = new_part.id;
      new_part.weight = 1.0f;
      group.parts.push_back(new_part);
    }
    ImGui::PopID();

    ed::EndNode();
    ed::PopStyleVar();

    group_index++;
  }

  // --- Render links ---
  int link_id_counter = 1;
  int target_group_idx = 0;
  std::unordered_map<uintptr_t, std::pair<int, std::string>> m_LinkIdToData;

  for (const auto& group : m_ModelData.selection_groups) {
    for (const auto& parent_id : group.parent) {
      int current_link_id = link_id_counter++;
      ed::LinkId linkId = current_link_id;

      ed::PinId outputPinId = HashString(parent_id);
      ed::PinId inputPinId = HashString(group.group_id + "_IN_" +
                                        std::to_string(target_group_idx));

      ed::Link(linkId, outputPinId, inputPinId, ImVec4(0.3f, 0.7f, 0.9f, 1.0f),
               2.0f);
      m_LinkIdToData[current_link_id] = {target_group_idx, parent_id};
    }
    target_group_idx++;
  }

  // --- Process user interactions for creating new links ---
  if (ed::BeginCreate()) {
    ed::PinId inputPinId, outputPinId;
    if (ed::QueryNewLink(&inputPinId, &outputPinId)) {
      int target_idx = -1;
      std::string sourcePartId = "";

      // Ensure connection flows from an output pin to an input pin
      if (m_InputPinToGroupIdx.count(inputPinId.Get()) &&
          m_PinIdToString.count(outputPinId.Get())) {
        target_idx = m_InputPinToGroupIdx[inputPinId.Get()];
        sourcePartId = m_PinIdToString[outputPinId.Get()];
      } else if (m_InputPinToGroupIdx.count(outputPinId.Get()) &&
                 m_PinIdToString.count(inputPinId.Get())) {
        target_idx = m_InputPinToGroupIdx[outputPinId.Get()];
        sourcePartId = m_PinIdToString[inputPinId.Get()];
      }

      if (target_idx != -1 && !sourcePartId.empty()) {
        if (ed::AcceptNewItem(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), 2.0f)) {
          // Append parent, multiple parents allowed
          auto& parents = m_ModelData.selection_groups[target_idx].parent;
          if (std::find(parents.begin(), parents.end(), sourcePartId) ==
              parents.end()) {
            parents.push_back(sourcePartId);
          }
        }
      } else {
        ed::RejectNewItem(ImVec4(1, 0, 0, 1), 2.0f);
      }
    }
  }
  ed::EndCreate();

  // --- Process user interactions for deleting existing links ---
  if (ed::BeginDelete()) {
    ed::LinkId deletedLinkId;
    if (ed::QueryDeletedLink(&deletedLinkId)) {
      if (ed::AcceptDeletedItem()) {
        if (m_LinkIdToData.count(deletedLinkId.Get())) {
          auto data = m_LinkIdToData[deletedLinkId.Get()];
          auto& parents = m_ModelData.selection_groups[data.first].parent;

          for (auto it = parents.begin(); it != parents.end(); ++it) {
            if (*it == data.second) {
              parents.erase(it);
              break;
            }
          }
        }
      }
    }
  }
  ed::EndDelete();

  // Frame the camera gracefully after the layout algorithm runs
  if (trigger_nav) {
    ed::NavigateToContent();
  }

  ed::End();

  // --- Draw the text overlay floating in the top-right corner of the canvas
  // view ---
  float zoom = ed::GetCurrentZoom();
  std::string zoom_text;
  if (std::abs(zoom - 1.0f) < 0.01f) {
    zoom_text = "Zoom 1:1";
  } else {
    zoom_text = "Zoom " +
                std::to_string(static_cast<int>(std::round(zoom * 100.0f))) +
                "%";
  }

  ImVec2 text_size = ImGui::CalcTextSize(zoom_text.c_str());
  ImVec2 text_pos = ImVec2(editor_pos.x + editor_size.x - text_size.x - 16.0f,
                           editor_pos.y + 16.0f);

  ImGui::GetWindowDrawList()->AddText(text_pos, IM_COL32(180, 180, 180, 255),
                                      zoom_text.c_str());

  ed::SetCurrentEditor(nullptr);
}
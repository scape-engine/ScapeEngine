#include "asset_browser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "editor/gui/inspectables/asset_inspectable.h"
#include "editor/runtime/runtime.h"
#include "engine/core/logger.h"
#include "engine/renderer/icons/icon_loader.h"

namespace Panels {

uint32_t AssetBrowserPanel::selected_folder_id_ = 0;
uint32_t AssetBrowserPanel::selected_node_id_ = 0;
std::vector<uint32_t> AssetBrowserPanel::history_;
int AssetBrowserPanel::history_pos_ = -1;

AssetBrowserPanel::AssetBrowserPanel()
    : observer_(Runtime::Project().GetObserver()),
      assets_(Runtime::Project().Assets()) {}

// Extract filename from path for display
std::string AssetBrowserPanel::Filename(
    const std::filesystem::directory_entry& e) {
  return e.path().filename().string();
}

AssetBrowserPanel::NodeUIData AssetBrowserPanel::NodeUIData::CreateFor(
    const std::string& name, float scale) {

  ImFont* font = ImGui::GetFont();

  ImVec2 padding{8.0f * scale, 6.0f * scale};
  ImVec2 icon_size{40.0f * scale, 40.0f * scale};

  // Fixed cell width so the grid stays aligned
  const float total_w = 84.0f * scale;

  // Label wraps inside the cell's horizontal padding
  const float wrap_width = total_w - padding.x * 2.0f;

  ImVec2 text_size =
      font->CalcTextSizeA(font->FontSize, FLT_MAX, wrap_width, name.c_str());

  // padding | icon | padding | label | padding
  const float total_h =
      padding.y + icon_size.y + padding.y + text_size.y + padding.y;

  return NodeUIData(name, font, padding, icon_size, text_size,
                    ImVec2(total_w, total_h));
}

AssetBrowserPanel::NodeUIData AssetBrowserPanel::MakeNodeUI(
    const std::string& name) const {
  return NodeUIData::CreateFor(name, icon_scale_);
}

// Draw panel
void AssetBrowserPanel::Draw() {
  HandleInputs();  // called once per frame

  // Get draw list and position
  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  ImVec2 position = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();

  // Render components
  ImVec2 nav_size = RenderBreadcrumbBar(draw_list, position);
  position.y += nav_size.y;

  ImVec2 folder_size = RenderSideFolders(draw_list, position);
  position.x += folder_size.x;

  ImVec2 content_size = ImVec2(size.x - folder_size.x, size.y - nav_size.y);

  RenderNodes(draw_list, position, content_size);
}

// Handle user input (zooming with Ctrl + wheel)
void AssetBrowserPanel::HandleInputs() {
  ImGuiIO& io = ImGui::GetIO();

  const float scale_speed = 0.10f;     // wheel sensitivity
  const float scale_smoothing = 6.5f;  // interpolation rate
  const float scale_min = 0.8f;        // hard limit
  const float scale_max = 4.0f;

  const bool window_focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
      ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

  // Capture input
  if (window_focused && io.KeyCtrl && io.MouseWheel != 0.0f) {
    const float step = icon_scale_ * scale_speed;
    target_icon_scale_ += (io.MouseWheel > 0.0f ? +step : -step);
  }

  // Clamp target and keep it within bounds
  target_icon_scale_ = std::clamp(target_icon_scale_, scale_min, scale_max);

  // Interpolation
  const float dt = io.DeltaTime;
  const float lerp = 1.0f - std::exp(-scale_smoothing * dt);  // exp-decay
  icon_scale_ += (target_icon_scale_ - icon_scale_) * lerp;
}

void AssetBrowserPanel::SelectFolder(uint32_t folder_id) {
  selected_node_id_ = 0;
  if (folder_id == selected_folder_id_)
    return;

  // Drop forward entries, then record the new location
  history_.resize(history_pos_ + 1);
  history_.push_back(folder_id);
  history_pos_ = static_cast<int>(history_.size()) - 1;

  selected_folder_id_ = folder_id;
}

void AssetBrowserPanel::UnselectFolder() {
  SelectFolder(0);
}

void AssetBrowserPanel::GoBack() {
  if (history_pos_ <= 0)
    return;
  selected_folder_id_ = history_[--history_pos_];
  selected_node_id_ = 0;
}

void AssetBrowserPanel::GoForward() {
  if (history_pos_ + 1 >= static_cast<int>(history_.size()))
    return;
  selected_folder_id_ = history_[++history_pos_];
  selected_node_id_ = 0;
}

bool AssetBrowserPanel::MatchFilter(const std::string& name) const {
  if (filter_[0] == '\0')
    return true;
  auto lower = [](std::string s) {
    for (char& c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  return lower(name).find(lower(filter_)) != std::string::npos;
}

void AssetBrowserPanel::OpenNodeInApplication(NodeRef node) {
  if (!node)
    return;

  // Get absolute path from project
  std::filesystem::path abs_path = Runtime::Project().AbsolutePath(node->path_);

  // Create system command based on platform
  std::string command;

#if defined(_WIN32) || defined(_WIN64)
  command = "start \"\" \"" + abs_path.string() + "\"";
#elif defined(__APPLE__)
  command = "open \"" + abs_path.string() + "\"";
#elif defined(__linux__)
  command = "xdg-open \"" + abs_path.string() + "\"";
#endif

  // Execute command if valid
  if (!command.empty()) {
    int result = std::system(command.c_str());
    if (result != 0) {
      Logger::getInstance().Log(LogLevel::Warning,
                                "Failed to open file: " + abs_path.string());
    }
  }
}

void AssetBrowserPanel::OpenNodeInExplorer(NodeRef node) {
  if (!node)
    return;

  // Get absolute path to parent directory
  std::filesystem::path dir_path =
      Runtime::Project().AbsolutePath(node->path_.parent_path());

  // Create system command based on platform
  std::string command;

#if defined(_WIN32) || defined(_WIN64)
  command = "explorer \"" + dir_path.string() + "\"";
#elif defined(__APPLE__)
  command = "open \"" + dir_path.string() + "\"";
#elif defined(__linux__)
  command = "xdg-open \"" + dir_path.string() + "\"";
#endif

  // Execute command if valid
  if (!command.empty()) {
    int result = std::system(command.c_str());
    if (result != 0) {
      Logger::getInstance().Log(
          LogLevel::Warning, "Failed to open directory: " + dir_path.string());
    }
  }
}

// ------------------------- TOP CONTAINER -------------------------
ImVec2 AssetBrowserPanel::RenderBreadcrumbBar(ImDrawList& draw_list,
                                              ImVec2 position) {
  const float height = top_bar_height_;
  const float pad = 8.0f;
  const float gap = 6.0f;
  const float arrow_w = 20.0f;
  const float control_h = EditorSizes::control_height + 4.0f;
  ImFont* small = EditorStyles::GetFonts().s;

  ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, height);
  const float y = position.y + (height - control_h) * 0.5f;
  const float right = position.x + size.x - pad;

  // BACK / FORWARD
  auto arrow = [&](float x, const char* glyph, bool enabled) {
    const ImVec2 min = ImVec2(x, y);
    const ImVec2 max = ImVec2(x + arrow_w, y + control_h);
    const bool hovered = enabled && ImGui::IsMouseHoveringRect(min, max);
    IMComponents::Glyph(draw_list, min, ImVec2(arrow_w, control_h), glyph,
                        !enabled  ? EditorColor::text_disabled
                        : hovered ? EditorColor::text_bright
                                  : EditorColor::text,
                        small);
    return hovered && ImGui::IsMouseClicked(0);
  };

  float x = position.x + pad;
  if (arrow(x, ICON_FA_CHEVRON_LEFT, history_pos_ > 0))
    GoBack();
  x += arrow_w;
  if (arrow(x, ICON_FA_CHEVRON_RIGHT,
            history_pos_ + 1 < static_cast<int>(history_.size())))
    GoForward();
  x += arrow_w + gap;

  // PATH TEXT ("project/sub/folder")
  auto folder = observer_.FetchFolder(selected_folder_id_);
  std::string path_text = "No folder selected";
  if (folder) {
    const std::filesystem::path project_root =
        Runtime::Project().AbsolutePath("").lexically_normal();
    path_text = Runtime::Project().ProjectName();
    for (const auto& part :
         std::filesystem::relative(folder->path_, project_root)) {
      if (part == "." || part == "..")
        continue;
      path_text += "/" + part.string();
    }
  }

  // PATH FIELD (read-only)
  const float path_w = (right - x - gap) * 0.5f;
  const ImVec2 path_min = ImVec2(x, y);
  const ImVec2 path_max = ImVec2(x + path_w, y + control_h);
  draw_list.AddRectFilled(path_min, path_max, EditorColor::input_bg,
                          EditorSizes::control_radius);
  draw_list.PushClipRect(path_min, ImVec2(path_max.x - 6.0f, path_max.y), true);
  draw_list.AddText(
      ImVec2(x + 8.0f, y + (control_h - ImGui::GetFontSize()) * 0.5f),
      EditorColor::text, path_text.c_str());
  draw_list.PopClipRect();
  x += path_w + gap;

  // FILTER FIELD
  IMComponents::SearchField(draw_list, "##AssetFilter", filter_,
                            IM_ARRAYSIZE(filter_), ImVec2(x, y),
                            ImVec2(right - x, control_h));

  return size;
}

// --------------------- LEFT CONTAINER ---------------------
ImVec2 AssetBrowserPanel::RenderSideFolders(ImDrawList& draw_list,
                                            ImVec2 position) {
  ImGui::SetCursorScreenPos(position);
  ImVec2 size = ImVec2(tree_width_, ImGui::GetContentRegionAvail().y);

  ImGui::BeginChild("FolderTree", size, false);
  ImDrawList& tree_draw = *ImGui::GetWindowDrawList();

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                      ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
  ImGui::Dummy(ImVec2(0.0f, 4.0f));

  auto root = observer_.RootNode();
  if (root)
    RenderSideFolder(tree_draw, root, 0);

  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::SetCursorScreenPos(position);

  return size;
}

void AssetBrowserPanel::RenderSideFolder(ImDrawList& draw_list,
                                         FolderRef folder,
                                         uint32_t indentation) {
  if (!folder)
    return;

  const float row_h = tree_row_height_;
  const float caret_w = 14.0f;
  const float icon_size = 14.0f;
  const float pad_x = 6.0f;
  ImFont* small = EditorStyles::GetFonts().s;

  const ImVec2 row_min = ImGui::GetCursorScreenPos();
  const ImVec2 row_max =
      ImVec2(row_min.x + ImGui::GetContentRegionAvail().x, row_min.y + row_h);
  const float x0 = row_min.x + pad_x + indentation * tree_indent_;

  const bool has_children = !folder->subfolders_.empty();
  const bool selected = folder->id_ == selected_folder_id_;

  auto toggle = [&]() {
    const_cast<ProjectObserver::Folder*>(folder.get())->expanded_ =
        !folder->expanded_;
  };

  // INTERACTION (row selects, chevron or double-click toggles)
  ImGui::InvisibleButton(("folder_" + std::to_string(folder->id_)).c_str(),
                         ImVec2(row_max.x - row_min.x, row_h));
  const bool hovered = ImGui::IsItemHovered();
  const bool caret_hovered =
      has_children &&
      ImGui::IsMouseHoveringRect(ImVec2(x0, row_min.y),
                                 ImVec2(x0 + caret_w, row_max.y));
  if (ImGui::IsItemClicked()) {
    if (caret_hovered)
      toggle();
    else
      SelectFolder(folder->id_);
  }
  if (hovered && has_children && !caret_hovered &&
      ImGui::IsMouseDoubleClicked(0))
    toggle();

  // ROW BACKGROUND
  if (selected)
    draw_list.AddRectFilled(row_min, row_max, EditorColor::control_selected,
                            EditorSizes::control_radius);
  else if (hovered)
    draw_list.AddRectFilled(row_min, row_max, EditorColor::hover_overlay,
                            EditorSizes::control_radius);

  // CHEVRON
  if (has_children)
    IMComponents::Glyph(
        draw_list, ImVec2(x0, row_min.y), ImVec2(caret_w, row_h),
        folder->expanded_ ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT,
        caret_hovered ? EditorColor::text_bright : EditorColor::chevron, small);

  // FOLDER ICON (tinted)
  const ImVec2 icon_min =
      ImVec2(std::floor(x0 + caret_w + 2.0f),
             std::floor(row_min.y + (row_h - icon_size) * 0.5f));
  draw_list.AddImage(IconLoader::ToImGuiTexture("folder"), icon_min,
                     ImVec2(icon_min.x + icon_size, icon_min.y + icon_size),
                     ImVec2(0, 0), ImVec2(1, 1), EditorColor::folder);

  // NAME
  draw_list.AddText(ImVec2(icon_min.x + icon_size + 6.0f,
                           row_min.y + (row_h - ImGui::GetFontSize()) * 0.5f),
                    selected ? EditorColor::text_bright : EditorColor::text,
                    folder->name_.c_str());

  // CHILDREN
  if (has_children && folder->expanded_)
    for (const auto& subfolder : folder->subfolders_)
      RenderSideFolder(draw_list, subfolder, indentation + 1);
}

// --------------------- RIGHT CONTAINER ---------------------
void AssetBrowserPanel::RenderNodes(ImDrawList& draw_list, ImVec2 position,
                                    ImVec2 size) {
  // Recessed rounded area, drawn on the parent list
  const ImVec2 box_min = ImVec2(position.x + 4.0f, position.y);
  const ImVec2 box_max = ImVec2(position.x + size.x, position.y + size.y);
  draw_list.AddRectFilled(box_min, box_max, EditorColor::strip,
                          content_rounding_);

  // Get current folder
  auto folder = observer_.FetchFolder(selected_folder_id_);
  if (!folder)
    return;

  ImGui::SetCursorScreenPos(box_min);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
  ImGui::BeginChild("ContentArea",
                    ImVec2(box_max.x - box_min.x, box_max.y - box_min.y),
                    false);
  ImGui::PopStyleColor();

  // Cells must draw into the child's list to clip and scroll correctly
  ImDrawList& content_draw = *ImGui::GetWindowDrawList();

  // Setup grid layout
  const ImVec2 padding(12.0f, 12.0f);
  const ImVec2 spacing(4.0f, 8.0f);

  ImVec2 cursor_pos = ImGui::GetCursorPos();
  cursor_pos.x += padding.x;
  cursor_pos.y += padding.y;
  ImVec2 grid_max = ImVec2(ImGui::GetContentRegionAvail().x - padding.x, 0);

  // Because multi-line text makes cards dynamic heights, we track the tallest
  // item in the current row to ensure the next row aligns cleanly below it
  float current_row_height = 0.0f;

  // Render all folders first
  for (const auto& subfolder : folder->subfolders_) {
    if (!MatchFilter(subfolder->name_))
      continue;
    NodeUIData ui_data = MakeNodeUI(subfolder->name_);

    // Check if we need to wrap to next row
    if (cursor_pos.x > padding.x &&
        cursor_pos.x + ui_data.total_size.x > grid_max.x) {
      cursor_pos.x = padding.x;
      cursor_pos.y += current_row_height + spacing.y;
      current_row_height = 0.0f;  // Reset height for new row
    }

    current_row_height = std::max(current_row_height, ui_data.total_size.y);

    // Set cursor and render node
    ImGui::SetCursorPos(cursor_pos);
    ImVec2 screen_pos = ImGui::GetCursorScreenPos();

    if (RenderNode(content_draw, subfolder, ui_data, screen_pos)) {
      cursor_pos.x += ui_data.total_size.x + spacing.x;
    }
  }

  // Then render all files
  for (const auto& file : folder->files_) {
    if (!MatchFilter(file->name_))
      continue;
    NodeUIData ui_data = MakeNodeUI(file->name_);

    // Check if we need to wrap to next row
    if (cursor_pos.x > padding.x &&
        cursor_pos.x + ui_data.total_size.x > grid_max.x) {
      cursor_pos.x = padding.x;
      cursor_pos.y += current_row_height + spacing.y;
      current_row_height = 0.0f;  // Reset height for new row
    }

    current_row_height = std::max(current_row_height, ui_data.total_size.y);

    // Set cursor and render node
    ImGui::SetCursorPos(cursor_pos);
    ImVec2 screen_pos = ImGui::GetCursorScreenPos();

    if (RenderNode(content_draw, file, ui_data, screen_pos)) {
      cursor_pos.x += ui_data.total_size.x + spacing.x;
    }
  }

  ImGui::EndChild();
}

bool AssetBrowserPanel::RenderNode(ImDrawList& draw_list, NodeRef node,
                                   const NodeUIData& ui_data, ImVec2 position) {
  if (!node)
    return false;

  // Skip file nodes without assets
  if (!node->IsFolder()) {
    auto file = std::static_pointer_cast<const ProjectObserver::File>(node);
    if (!file->asset_id_)
      return false;
  }

  // Interactive area bounds
  ImVec2 p0 = position;
  ImVec2 p1 = ImVec2(position.x + ui_data.total_size.x,
                     position.y + ui_data.total_size.y);

  ImGui::SetCursorScreenPos(position);
  ImGui::InvisibleButton(("node_" + std::to_string(node->id_)).c_str(),
                         ui_data.total_size);

  bool hovered = ImGui::IsItemHovered();
  bool clicked = ImGui::IsItemClicked();
  bool double_clicked =
      ImGui::IsItemClicked() && ImGui::GetIO().MouseDoubleClicked[0];
  bool selected = node->id_ == selected_node_id_;
  bool is_folder = node->IsFolder();

  // visual highligting
  ImU32 card_bg_color =
      hovered ? IM_COL32(60, 60, 60, 255) : IM_COL32(35, 35, 35, 255);
  float rounding = 6.0f;

  // Draw base card background
  draw_list.AddRectFilled(p0, p1, card_bg_color, rounding);

  // Draw text area footer background (darker)
  float text_bg_height = ui_data.text_size.y + ui_data.padding.y * 1.5f;
  ImVec2 text_bg_p0 = ImVec2(p0.x, p1.y - text_bg_height);
  draw_list.AddRectFilled(text_bg_p0, p1, IM_COL32(22, 22, 22, 255), rounding,
                          ImDrawFlags_RoundCornersBottom);

  // Asset type color strip
  ImU32 strip_color = IM_COL32(100, 100, 100, 255);  // Default dark gray
  if (!is_folder) {
    std::string ext = node->path_.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Assign UE5-inspired colors to extensions
    if (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj") {
      strip_color = IM_COL32(45, 180, 200, 255);  // Cyan (Static Meshes)
    } else if (ext == ".json") {
      strip_color = IM_COL32(200, 180, 45, 255);  // Yellow (Data/JSON)
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".tga") {
      strip_color = IM_COL32(180, 45, 45, 255);  // Red (Textures)
    } else if (ext == ".mat" || ext == ".material") {
      strip_color = IM_COL32(45, 200, 45, 255);  // Green (Materials)
    }
  } else {
    strip_color = IM_COL32(60, 60, 60, 255);  // Subtle gray for folders
  }

  // Draw the 2px color strip above the text background
  draw_list.AddRectFilled(ImVec2(p0.x, text_bg_p0.y),
                          ImVec2(p1.x, text_bg_p0.y + 2.0f), strip_color);

  // Draw Outline (Selection / Hover Border)
  if (selected) {
    // UE5 Blue outline for selected items
    draw_list.AddRect(p0, p1, IM_COL32(0, 112, 224, 255), rounding, 0, 2.0f);
  } else if (hovered) {
    // Subtle gray outline for hover
    draw_list.AddRect(p0, p1, IM_COL32(100, 100, 100, 255), rounding, 0, 1.0f);
  }

  // icon rendering
  // Center the icon strictly in the top section of the card
  float upper_height = p1.y - p0.y - text_bg_height;
  ImVec2 icon_pos =
      ImVec2(position.x + (ui_data.total_size.x - ui_data.icon_size.x) * 0.5f,
             position.y + (upper_height - ui_data.icon_size.y) * 0.5f);

  // Get appropriate icon
  uint32_t icon_handle;
  bool is_loading = false;

  if (node->IsFolder()) {
    // Use folder icon
    icon_handle = IconLoader::GetIconHandle("folder");
  } else {
    // Get file node and asset
    auto file = std::static_pointer_cast<const ProjectObserver::File>(node);
    auto asset = assets_.Get(file->asset_id_);

    if (asset) {
      // Use asset's icon if available
      icon_handle = asset->GetIcon();
      is_loading = asset->IsLoading();
    } else {
      // Fallback to generic file icon
      icon_handle = IconLoader::GetIconHandle("file");
    }
  }

  // Draw icon with white color
  ImU32 icon_color =
      is_loading ? IM_COL32(255, 255, 255, 120) : IM_COL32(255, 255, 255, 255);
  draw_list.AddImage((ImTextureID)(intptr_t)icon_handle, icon_pos,
                     ImVec2(icon_pos.x + ui_data.icon_size.x,
                            icon_pos.y + ui_data.icon_size.y),
                     ImVec2(0, 0), ImVec2(1, 1), icon_color);

  // label rendering (Centered in the footer background)
  float wrap_width = ui_data.total_size.x - (ui_data.padding.x * 1.5f);

  ImVec2 text_pos =
      ImVec2(position.x + (ui_data.total_size.x - ui_data.text_size.x) * 0.5f,
             text_bg_p0.y + (text_bg_height - ui_data.text_size.y) * 0.5f);

  // Pass wrap_width to AddText so it naturally splits the lines!
  draw_list.AddText(ui_data.font, ui_data.font->FontSize, text_pos,
                    IM_COL32(230, 230, 230, 255), ui_data.text.c_str(), nullptr,
                    wrap_width);

  // Handle interactions
  if (clicked) {
    // Select this node
    selected_node_id_ = node->id_;

    // If it's a file, create and show inspector
    if (!node->IsFolder()) {
      auto file = std::static_pointer_cast<const ProjectObserver::File>(node);
      if (file->asset_id_) {
        // TODO: Show asset in inspector
        // InsightPanelWindow::showAsset(file->asset_id_);
      }
    }
  }

  if (double_clicked) {
    if (node->IsFolder()) {
      // Enter folder
      SelectFolder(node->id_);
    } else {
      // Open file in application
      OpenNodeInApplication(node);
    }
  }

  // Handle middle click to open in explorer
  if (ImGui::IsItemClicked(2)) {
    OpenNodeInExplorer(node);
  }

  if (!node->IsFolder()) {
    auto file = std::static_pointer_cast<const ProjectObserver::File>(node);
    const std::string extension = node->path_.extension().string();
    const bool is_gltf = (extension == ".gltf" || extension == ".glb");

    // Drag operation for glTF files
    // TODO: refactor to handle other file types dynamically
    if (is_gltf &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      // Set payload as file path
      std::string path_str = node->path_.string();

      ImGui::SetDragDropPayload("GLTF_FILE", path_str.c_str(),
                                path_str.size() + 1);
      Logger::getInstance().Log(LogLevel::Debug, "Drag payload = " + path_str);

      // Preview content while dragging
      ImGui::Text("Import %s", node->path_.filename().string().c_str());
      ImGui::EndDragDropSource();
    }
  }

  return true;
}

// Singleton accessor for EditorUI
void AssetBrowser() {

  static AssetBrowserPanel panel;
  panel.Draw();
}

}  // namespace Panels
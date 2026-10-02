#ifndef ASSET_BROWSER_H
#define ASSET_BROWSER_H

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

#include "editor/project/project_observer.h"
#include "editor/systems/editor_asset.h"

#include "editor/gui/window_base.h"

namespace Panels {

using NodeRef = std::shared_ptr<ProjectObserver::IONode>;
using FileRef = std::shared_ptr<ProjectObserver::File>;
using FolderRef = std::shared_ptr<ProjectObserver::Folder>;

class AssetBrowserPanel {

public:
  AssetBrowserPanel();

  // Draw panel
  void Draw();

  static void SelectFolder(
      uint32_t folderId);  // placeholder, select folder by node id

  static void UnselectFolder();  // placeholder, unselect folder

private:
  // UI layout data
  struct NodeUIData {
    std::string text;
    ImFont* font;
    ImVec2 padding;
    ImVec2 icon_size;
    ImVec2 text_size;
    ImVec2 total_size;

    NodeUIData() = default;
    NodeUIData(std::string text_, ImFont* font_, ImVec2 padding_,
               ImVec2 icon_size_, ImVec2 text_size_, ImVec2 total_size_)
        : text(std::move(text_)),
          font(font_),
          padding(padding_),
          icon_size(icon_size_),
          text_size(text_size_),
          total_size(total_size_) {}

    static NodeUIData CreateFor(const std::string& name, float scale);
  };

  // References to global project components
  ProjectObserver& observer_;
  ProjectAssets& assets_;

  static uint32_t selected_folder_id_;
  static uint32_t selected_node_id_;

  // Navigation history (back - forward arrows)
  static std::vector<uint32_t> history_;
  static int history_pos_;
  static void GoBack();
  static void GoForward();

  // True if name passes the filter field
  bool MatchFilter(const std::string& name) const;

  // Prepare UI display data for node with specified name
  NodeUIData MakeNodeUI(const std::string& name) const;

  // ----- RENDERING FUNCTIONS -----
  ImVec2 RenderBreadcrumbBar(ImDrawList& draw_list, ImVec2 position);
  ImVec2 RenderSideFolders(ImDrawList& draw_list, ImVec2 position);
  void RenderSideFolder(ImDrawList& draw_list, FolderRef folder,
                        uint32_t indentation);
  void RenderNodes(ImDrawList& draw_list, ImVec2 position, ImVec2 size);
  bool RenderNode(ImDrawList& draw_list, NodeRef node,
                  const NodeUIData& ui_data, ImVec2 position);

  // ----- NODE UTILITIES -----
  void OpenNodeInApplication(NodeRef node);
  void OpenNodeInExplorer(NodeRef node);

  // ----- DATA -----
  char filter_[128]{};  // search filter

  float icon_scale_{1.0f};         // on-screen scale
  float target_icon_scale_{1.0f};  // scale modifier

  // ----- UTILITIES -----
  void HandleInputs();  // Handle user input
  static std::string Filename(
      const std::filesystem::directory_entry& e);  // Extract filename from path
};

// ----- LAYOUT (px) -----
static constexpr float top_bar_height_ = 36.0f;
static constexpr float tree_width_ = 200.0f;
static constexpr float tree_row_height_ = 20.0f;
static constexpr float tree_indent_ = 16.0f;
static constexpr float content_rounding_ = 6.0f;

// Singleton accessor for EditorUI
void AssetBrowser();

}  // namespace Panels
#endif  // ASSET_BROWSER_H

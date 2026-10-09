#ifndef TAB_CONTAINER_H
#define TAB_CONTAINER_H

#include <memory>
#include <string>
#include <vector>

#include <boost/signals2.hpp>

#include "window_base.h"

//---------------------------------------------------------------------------
// TAB CONTAINER
// - Owns a set of panels and shows exactly one of them, docked into a nested
//   tab-less dockspace, selected through a Blender-style vertical icon rail.
// - Hosted panels are ordinary WindowBase implementations; they don't know
//   they're hosted.
//---------------------------------------------------------------------------
struct TabEntry {
  const char* icon;
  const char* window_name;
  const char* tooltip;
  std::unique_ptr<WindowBase> panel;
  int group = 0;  // consecutive entries with different groups get a wider gap
  const char* icon_id = nullptr;  // IconLoader id;
};

class TabContainer : public WindowBase {
public:
  TabContainer(std::string name, std::vector<TabEntry> entries);

  void Render() override;

  // Select hosted panel by window name
  void Select(const char* window_name);

private:
  void RenderHeader(ImDrawList& draw_list, float width);
  void RenderRail(ImDrawList& draw_list, ImVec2 rail_min, float height);
  void RenderActivePanel(ImVec2 position, ImVec2 size);

  void ApplyCollapse();

  // Layout (px)
  static constexpr float header_height_ = 30.0f;
  static constexpr float header_pad_x_ = 10.0f;
  static constexpr float control_height_ = 20.0f;
  static constexpr float rail_width_ = 32.0f;
  static constexpr float rail_button_ = 24.0f;
  static constexpr float rail_pad_y_ = 8.0f;
  static constexpr float rail_gap_ = 2.0f;
  static constexpr float rail_group_gap_ = 12.0f;

  // Collapse state
  bool collapsed_ = true;
  bool animating_ = false;
  float expanded_width_ = 300.0f;
  float current_width_ = rail_width_;

  std::string name_;
  std::vector<TabEntry> entries_;
  size_t active_ = 0;
  ImGuiID dock_id_ = 0;
  char search_buffer_[128] = "";
};

#endif  // TAB_CONTAINER_H
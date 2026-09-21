#ifndef TAB_CONTAINER_H
#define TAB_CONTAINER_H

#include <string>
#include <vector>
#include <memory>

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
};

class TabContainer : public WindowBase {
public:
  TabContainer(std::string name, std::vector<TabEntry> entries);

  void Render() override;

  // Select hosted panel by window name
  void Select(const char* window_name);

private:
  void RenderHeader(ImDrawList& draw_list);
  void RenderRail(ImDrawList& draw_list, ImVec2 rail_min, float height);
  void RenderActivePanel(ImVec2 position, ImVec2 size);

  // Layout (px)
  static constexpr float header_height_ = 42.0f;
  static constexpr float header_pad_x_ = 10.0f;
  static constexpr float control_height_ = 26.0f;
  static constexpr float rail_width_ = 48.0f;
  static constexpr float rail_button_ = 36.0f;
  static constexpr float rail_pad_y_ = 14.0f;
  static constexpr float rail_gap_ = 4.0f;
  static constexpr float rail_group_gap_ = 18.0f;

  std::string name_;
  std::vector<TabEntry> entries_;
  size_t active_ = 0;
  ImGuiID dock_id_ = 0;
  char search_buffer_[128] = "";
};

#endif  // TAB_CONTAINER_H
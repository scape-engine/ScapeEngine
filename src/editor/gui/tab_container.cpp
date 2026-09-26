#include "tab_container.h"

#include <cstring>

#include "editor/events.h"

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/utils/gui_utils.h"

TabContainer::TabContainer(std::string name, std::vector<TabEntry> entries)
    : name_(std::move(name)), entries_(std::move(entries)) {}

void TabContainer::Select(const char* window_name) {
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (std::strcmp(entries_[i].window_name, window_name) == 0) {
      active_ = i;
      return;
    }
  }
}

void TabContainer::Render() {
  GUIUtils::HideDockTabBar();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin(name_.c_str(), nullptr, EditorFlag::standard);
  ImGui::PopStyleVar();

  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  RenderHeader(draw_list);

  const ImVec2 body_min = ImGui::GetCursorScreenPos();
  const ImVec2 body_size = ImGui::GetContentRegionAvail();
  RenderRail(draw_list, body_min, body_size.y);

  const ImVec2 host_pos = ImVec2(body_min.x + rail_width_, body_min.y);
  const ImVec2 host_size = ImVec2(body_size.x - rail_width_, body_size.y);
  ImGui::SetCursorScreenPos(host_pos);
  dock_id_ = ImGui::GetID("##HostedPanels");
  GUIUtils::HostDockSpace(dock_id_, host_size);

  ImGui::End();

  // Hosted panel is a separate top-level window; submit it after ours
  RenderActivePanel(host_pos, host_size);
}

void TabContainer::RenderActivePanel(ImVec2 position, ImVec2 size) {
  if (entries_.empty())
    return;
  if (active_ >= entries_.size())
    active_ = 0;

  // Only the active panel is submitted; ImGui drops the others from the node,
  // so the tab-less node always shows this one.
  GUIUtils::DockNextWindowInto(dock_id_);
  entries_[active_].panel->Render();
}

//=============================================================================
// HEADER   [icon v]                          [ (q) Search... ] [v]
//=============================================================================
void TabContainer::RenderHeader(ImDrawList& draw_list) {
  const ImVec2 strip_min = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const ImVec2 strip_max =
      ImVec2(strip_min.x + width, strip_min.y + header_height_);
  const float y = strip_min.y + (header_height_ - control_height_) * 0.5f;

  // Editor-type selector (placeholder)
  IMComponents::DropdownButton(draw_list, "##ContainerType", ICON_FA_SLIDERS,
                               ImVec2(strip_min.x + header_pad_x_, y),
                               control_height_);

  // Right cluster: more-button, search to its left
  const float more_x = strip_max.x - header_pad_x_ - control_height_;
  IMComponents::DropdownButton(draw_list, "##ContainerMore", nullptr,
                               ImVec2(more_x, y), control_height_);

  const float search_width = width * 0.42f;
  IMComponents::SearchField(draw_list, "##ContainerSearch", search_buffer_,
                            sizeof(search_buffer_),
                            ImVec2(more_x - 6.0f - search_width, y),
                            ImVec2(search_width, control_height_));

  draw_list.AddLine(ImVec2(strip_min.x, strip_max.y - 0.5f),
                    ImVec2(strip_max.x, strip_max.y - 0.5f),
                    EditorColor::panel_stroke, 1.0f);

  ImGui::SetCursorScreenPos(ImVec2(strip_min.x, strip_max.y));
}

//=============================================================================
// RAIL (vertical tabs)
//=============================================================================
void TabContainer::RenderRail(ImDrawList& draw_list, ImVec2 rail_min,
                              float height) {
  const float x = rail_min.x + (rail_width_ - rail_button_) * 0.5f;
  const ImVec2 button_size = ImVec2(rail_button_, rail_button_);
  float y = rail_min.y + rail_pad_y_;
  int last_group = entries_.empty() ? 0 : entries_.front().group;

  for (size_t i = 0; i < entries_.size(); ++i) {
    const TabEntry& entry = entries_[i];
    if (entry.group != last_group) {
      y += rail_group_gap_ - rail_gap_;
      last_group = entry.group;
    }

    const ImVec2 p0 = ImVec2(x, y);
    const ImVec2 p1 = p0 + button_size;

    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton(entry.window_name, button_size);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
      active_ = i;
    if (hovered)
      IMComponents::Tooltip(entry.tooltip);

    // Square: recessed dark by default, lifted when selected
    const bool selected = i == active_;
    const ImU32 bg = selected  ? EditorColor::control_selected
                     : hovered ? EditorColor::hover_overlay
                               : IM_COL32(0, 0, 0, 0);
    if (bg != 0)
      draw_list.AddRectFilled(p0, p1, bg, EditorSizes::control_radius);

    // Icon: small glyph centered in the square
    IMComponents::Glyph(draw_list, p0, button_size, entry.icon,
                        selected ? EditorColor::text_bright : EditorColor::text,
                        EditorStyles::GetFonts().s);

    y += rail_button_ + rail_gap_;
  }

  const float divider_x = rail_min.x + rail_width_ - 0.5f;
  draw_list.AddLine(ImVec2(divider_x, rail_min.y),
                    ImVec2(divider_x, rail_min.y + height),
                    EditorColor::panel_stroke, 1.0f);
}
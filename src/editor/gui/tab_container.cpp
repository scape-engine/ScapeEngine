#include "tab_container.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

#include "engine/renderer/icons/icon_loader.h"

#include "editor/events.h"

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/utils/gui_utils.h"

namespace {

// Tab button background: selected > hovered > none (0 = draw nothing)
ImU32 TabBackground(bool selected, bool hovered) {
  return selected  ? EditorColor::surface_4
         : hovered ? EditorColor::hover_overlay
                   : 0;
}

}  // namespace

TabContainer::TabContainer(std::string name, std::vector<TabEntry> entries,
                           ImGuiDir side, bool start_collapsed)
    : name_(std::move(name)), entries_(std::move(entries)) {
  side_ = side;
  collapsed_ = start_collapsed;
  expanded_width_ = Horizontal() ? 220.0f : 300.0f;
  current_width_ = collapsed_ ? CollapsedSize() : expanded_width_;
}

float TabContainer::CollapsedSize() const {
  return Horizontal() ? strip_height_ : rail_width_;
}

void TabContainer::ActivateTab(size_t index) {
  collapsed_ = (index == active_ && !collapsed_);
  active_ = index;
}

void TabContainer::Select(const char* window_name) {
  for (size_t i = 0; i < entries_.size(); ++i) {
    if (std::strcmp(entries_[i].window_name, window_name) == 0) {
      active_ = i;
      collapsed_ = false;
      return;
    }
  }
}

void TabContainer::Render() {
  ApplyCollapse();

  // Reserve this edge of the main viewport
  const float margin = EditorSizes::panel_margin;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
  const bool open = ImGui::BeginViewportSideBar(
      name_.c_str(), ImGui::GetMainViewport(), side_, current_width_ + margin,
      EditorFlag::standard | ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  if (!open) {
    ImGui::End();
    return;
  }

  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  const ImVec2 strip_pos = ImGui::GetWindowPos();
  const ImVec2 strip_size = ImGui::GetWindowSize();

  // Card inside the reserved strip
  ImVec2 win_min;
  ImVec2 win_size;
  if (Horizontal()) {
    // Bottom: outer margin left and bottom, inner gap toward the right panel
    win_min = ImVec2(strip_pos.x + margin, strip_pos.y);
    win_size =
        ImVec2(strip_size.x - margin - EditorSizes::panel_gap, current_width_);
  } else {
    // Right: outer margin right, top and bottom
    win_min = ImVec2(strip_pos.x, strip_pos.y + margin);
    win_size = ImVec2(current_width_, strip_size.y - margin * 2.0f);
  }
  draw_list.AddRectFilled(win_min, win_min + win_size, EditorColor::surface_2,
                          EditorSizes::panel_radius);

  // RESIZE EDGE (inner side, only while expanded)
  if (!collapsed_) {
    ImGui::SetCursorScreenPos(win_min);
    ImGui::InvisibleButton("##ResizeEdge", Horizontal()
                                               ? ImVec2(win_size.x, 4.0f)
                                               : ImVec2(4.0f, win_size.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
      ImGui::SetMouseCursor(Horizontal() ? ImGuiMouseCursor_ResizeNS
                                         : ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) {
      const ImVec2 delta = ImGui::GetIO().MouseDelta;
      const float min_size = Horizontal() ? 120.0f : 200.0f;
      expanded_width_ =
          std::clamp(expanded_width_ - (Horizontal() ? delta.y : delta.x),
                     min_size, 600.0f);
      current_width_ = expanded_width_;
    }
  }

  // TABS: vertical rail (right) or horizontal strip (bottom)
  if (Horizontal())
    RenderStrip(draw_list, win_min, win_size.x);
  else
    RenderRail(draw_list, win_min, win_size.y);

  dock_id_ = ImGui::GetID("##HostedPanels");
  if (collapsed_ || animating_) {
    // Keep the hosted dockspace alive so panel stays docked
    ImGui::DockSpace(dock_id_, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
    ImGui::End();
    return;
  }

  ImVec2 host_pos;
  ImVec2 host_size;
  if (Horizontal()) {
    // Strip doubles as header; panel sits right below
    host_pos = ImVec2(win_min.x, win_min.y + strip_height_);
    host_size = ImVec2(win_size.x, win_size.y - strip_height_);
  } else {
    ImGui::SetCursorScreenPos(ImVec2(win_min.x + rail_width_, win_min.y));
    RenderHeader(draw_list, win_size.x - rail_width_);
    host_pos = ImGui::GetCursorScreenPos();
    host_size = ImVec2(win_min.x + win_size.x - host_pos.x,
                       win_min.y + win_size.y - host_pos.y);
  }
  ImGui::SetCursorScreenPos(host_pos);
  GUIUtils::HostDockSpace(dock_id_, host_size);

  ImGui::End();

  // Hosted panel is a separate top-level window
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
// HEADER   [icon v]
//=============================================================================
void TabContainer::RenderHeader(ImDrawList& draw_list, float width) {
  const ImVec2 strip_min = ImGui::GetCursorScreenPos();
  const ImVec2 strip_max =
      ImVec2(strip_min.x + width, strip_min.y + header_height_);
  const float y = strip_min.y + (header_height_ - control_height_) * 0.5f;

  const float button_w = std::floor(control_height_ * 2.0f);  // icon + chevron
  const float gap = 6.0f;

  // Left
  const float type_x = strip_min.x + header_pad_x_;
  IMComponents::DropdownButton(draw_list, "##ContainerType", ICON_FA_SLIDERS,
                               ImVec2(type_x, y), control_height_, true,
                               button_w);

  // Right
  const float more_x = strip_max.x - header_pad_x_ - control_height_;
  IMComponents::DropdownButton(draw_list, "##ContainerMore", nullptr,
                               ImVec2(more_x, y), control_height_, true,
                               control_height_);

  // Search fills the space between them
  const float search_x = type_x + button_w + gap;
  IMComponents::SearchField(
      draw_list, "##ContainerSearch", search_buffer_, sizeof(search_buffer_),
      ImVec2(search_x, y),
      ImVec2(std::max(0.0f, more_x - gap - search_x), control_height_));

  draw_list.AddLine(ImVec2(strip_min.x, strip_max.y - 0.5f),
                    ImVec2(strip_max.x, strip_max.y - 0.5f),
                    EditorColor::line_subtle, 1.0f);

  ImGui::SetCursorScreenPos(ImVec2(strip_min.x, strip_max.y));
}

//=============================================================================
// RAIL (vertical tabs)
//=============================================================================
void TabContainer::RenderRail(ImDrawList& draw_list, ImVec2 rail_min,
                              float height) {
  const float x = rail_min.x + (rail_width_ - rail_button_) * 0.5f;
  const ImVec2 button_size = ImVec2(rail_button_, rail_button_);

  // COLLAPSE TOGGLE
  if (IMComponents::GlyphButton(draw_list, "##CollapseToggle", CollapseGlyph(),
                                ImVec2(x, rail_min.y + rail_pad_y_),
                                button_size, EditorColor::chevron,
                                collapsed_ ? "Expand panel" : "Collapse panel"))
    collapsed_ = !collapsed_;

  float y = rail_min.y + rail_pad_y_ + rail_button_ + rail_group_gap_;
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
      ActivateTab(i);
    if (hovered)
      IMComponents::Tooltip(entry.tooltip);

    // Set background colour
    const bool selected = i == active_;
    const ImU32 bg = selected  ? EditorColor::surface_4
                     : hovered ? EditorColor::surface_3
                               : EditorColor::surface_0;
    draw_list.AddRectFilled(p0, p1, bg, EditorSizes::control_radius);

    IMComponents::Icon(draw_list, entry.icon_id, entry.icon, p0, button_size,
                       selected ? EditorColor::text_bright : EditorColor::text);

    // Badge: dot in the top-right corner, drawn last so nothing covers it
    if (entry.badge && entry.badge() > 0)
      draw_list.AddCircleFilled(ImVec2(p1.x - 4.0f, p0.y + 4.0f), 3.0f,
                                EditorColor::error);

    y += rail_button_ + rail_gap_;
  }

  // Rail/content separator: only meaningful while the panel is wider than
  // the rail (collapsed, it would sit on the card's outer edge)
  if (current_width_ > rail_width_ + 1.0f) {
    const float divider_x = rail_min.x + rail_width_ - 0.5f;
    draw_list.AddLine(ImVec2(divider_x, rail_min.y),
                      ImVec2(divider_x, rail_min.y + height),
                      EditorColor::line_subtle, 1.0f);
  }
}

//=============================================================================
// STRIP (horizontal tabs, bottom side)   [˅] [icon Console] [icon Assets]
//=============================================================================
void TabContainer::RenderStrip(ImDrawList& draw_list, ImVec2 strip_min,
                               float width) {
  ImFont* small = EditorStyles::GetFonts().s;
  const float y = strip_min.y + (strip_height_ - rail_button_) * 0.5f;
  const ImVec2 button_size = ImVec2(rail_button_, rail_button_);
  const float icon_size = EditorSizes::s_icon_size;
  const float tab_pad = 8.0f;
  const float icon_gap = 6.0f;

  // COLLAPSE TOGGLE
  float x = strip_min.x + (rail_width_ - rail_button_) * 0.5f;
  if (IMComponents::GlyphButton(draw_list, "##CollapseToggle", CollapseGlyph(),
                                ImVec2(x, y), button_size, EditorColor::chevron,
                                collapsed_ ? "Expand panel" : "Collapse panel"))
    collapsed_ = !collapsed_;
  x += rail_button_ + rail_group_gap_;

  // TABS: icon + label
  for (size_t i = 0; i < entries_.size(); ++i) {
    const TabEntry& entry = entries_[i];
    const uint32_t count = entry.badge ? entry.badge() : 0;
    const ImVec2 badge_size = IMComponents::CountBadgeSize(count);
    const float badge_gap = 6.0f;
    const float badge_w = count > 0 ? badge_gap + badge_size.x : 0.0f;

    const ImVec2 label_size =
        small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, entry.window_name);
    const float tab_w = std::floor(tab_pad * 2.0f + icon_size + icon_gap +
                                   label_size.x + badge_w);
    const ImVec2 p0 = ImVec2(x, y);
    const ImVec2 p1 = p0 + ImVec2(tab_w, rail_button_);

    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton(entry.window_name, ImVec2(tab_w, rail_button_));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
      ActivateTab(i);

    const bool selected = i == active_;
    if (const ImU32 bg = TabBackground(selected, hovered))
      draw_list.AddRectFilled(p0, p1, bg, EditorSizes::control_radius);

    const ImU32 fg = selected ? EditorColor::text_bright : EditorColor::text;
    IMComponents::Icon(draw_list, entry.icon_id, entry.icon,
                       ImVec2(x + tab_pad, y), ImVec2(icon_size, rail_button_),
                       fg);

    const float text_x = x + tab_pad + icon_size + icon_gap;
    draw_list.AddText(
        small, small->FontSize,
        ImVec2(text_x, std::floor(y + (rail_button_ - label_size.y) * 0.5f)),
        fg, entry.window_name);

    IMComponents::CountBadge(draw_list, count,
                             ImVec2(text_x + label_size.x + badge_gap,
                                    y + (rail_button_ - badge_size.y) * 0.5f));

    x += tab_w + rail_gap_;
  }

  // Divider under strip while the panel is open
  if (!collapsed_)
    draw_list.AddLine(
        ImVec2(strip_min.x, strip_min.y + strip_height_ - 0.5f),
        ImVec2(strip_min.x + width, strip_min.y + strip_height_ - 0.5f),
        EditorColor::line_subtle, 1.0f);
}

void TabContainer::ApplyCollapse() {
  const float target = collapsed_ ? CollapsedSize() : expanded_width_;
  const float t = 1.0f - std::exp(-14.0f * ImGui::GetIO().DeltaTime);
  current_width_ += (target - current_width_) * t;
  animating_ = std::fabs(target - current_width_) >= 0.5f;
  if (!animating_)
    current_width_ = target;
}

const char* TabContainer::CollapseGlyph() const {
  if (Horizontal())
    return collapsed_ ? ICON_FA_CHEVRON_UP : ICON_FA_CHEVRON_DOWN;
  return collapsed_ ? ICON_FA_CHEVRON_LEFT : ICON_FA_CHEVRON_RIGHT;
}
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
  draw_list.AddRectFilled(win_min, win_min + win_size, EditorColor::panel,
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

  // COLLAPSE TOGGLE
  {
    const ImVec2 p0 = ImVec2(x, rail_min.y + rail_pad_y_);
    ImGui::SetCursorScreenPos(p0);
    if (ImGui::InvisibleButton("##CollapseToggle", button_size))
      collapsed_ = !collapsed_;
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
      IMComponents::Tooltip(collapsed_ ? "Expand panel" : "Collapse panel");
      draw_list.AddRectFilled(p0, p0 + button_size, EditorColor::hover_overlay,
                              EditorSizes::control_radius);
    }
    IMComponents::Glyph(
        draw_list, p0, button_size,
        collapsed_ ? ICON_FA_CHEVRON_LEFT : ICON_FA_CHEVRON_RIGHT,
        EditorColor::chevron, EditorStyles::GetFonts().s);
  }

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

    // Square: recessed dark by default, lifted when selected
    const bool selected = i == active_;
    const ImU32 bg = selected  ? EditorColor::control_selected
                     : hovered ? EditorColor::hover_overlay
                               : IM_COL32(0, 0, 0, 0);
    if (bg != 0)
      draw_list.AddRectFilled(p0, p1, bg, EditorSizes::control_radius);

    // Icon: small glyph centered in the square
    const ImU32 icon_color =
        selected ? EditorColor::text_bright : EditorColor::text;
    if (entry.icon_id) {
      const float icon_size = EditorSizes::s_icon_size;
      const ImVec2 icon_min =
          ImVec2(std::floor(p0.x + (rail_button_ - icon_size) * 0.5f),
                 std::floor(p0.y + (rail_button_ - icon_size) * 0.5f));
      draw_list.AddImage(IconLoader::ToImGuiTexture(entry.icon_id), icon_min,
                         icon_min + ImVec2(icon_size, icon_size), ImVec2(0, 0),
                         ImVec2(1, 1), icon_color);
    } else {
      IMComponents::Glyph(draw_list, p0, button_size, entry.icon, icon_color,
                          EditorStyles::GetFonts().s);
    }

    y += rail_button_ + rail_gap_;
  }

  const float divider_x = rail_min.x + rail_width_ - 0.5f;
  draw_list.AddLine(ImVec2(divider_x, rail_min.y),
                    ImVec2(divider_x, rail_min.y + height),
                    EditorColor::panel_stroke, 1.0f);
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
  {
    const ImVec2 p0 = ImVec2(x, y);
    ImGui::SetCursorScreenPos(p0);
    if (ImGui::InvisibleButton("##CollapseToggle", button_size))
      collapsed_ = !collapsed_;
    if (ImGui::IsItemHovered()) {
      IMComponents::Tooltip(collapsed_ ? "Expand panel" : "Collapse panel");
      draw_list.AddRectFilled(p0, p0 + button_size, EditorColor::hover_overlay,
                              EditorSizes::control_radius);
    }
    IMComponents::Glyph(draw_list, p0, button_size,
                        collapsed_ ? ICON_FA_CHEVRON_UP : ICON_FA_CHEVRON_DOWN,
                        EditorColor::chevron, small);
    x += rail_button_ + rail_group_gap_;
  }

  // TABS: icon + label
  for (size_t i = 0; i < entries_.size(); ++i) {
    const TabEntry& entry = entries_[i];
    const ImVec2 label_size =
        small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, entry.window_name);
    const float tab_w =
        std::floor(tab_pad * 2.0f + icon_size + icon_gap + label_size.x);
    const ImVec2 p0 = ImVec2(x, y);
    const ImVec2 p1 = p0 + ImVec2(tab_w, rail_button_);

    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton(entry.window_name, ImVec2(tab_w, rail_button_));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
      ActivateTab(i);

    const bool selected = i == active_;
    const ImU32 bg = selected  ? EditorColor::control_selected
                     : hovered ? EditorColor::hover_overlay
                               : IM_COL32(0, 0, 0, 0);
    if (bg != 0)
      draw_list.AddRectFilled(p0, p1, bg, EditorSizes::control_radius);

    const ImU32 fg = selected ? EditorColor::text_bright : EditorColor::text;
    const ImVec2 icon_min =
        ImVec2(std::floor(x + tab_pad),
               std::floor(y + (rail_button_ - icon_size) * 0.5f));
    if (entry.icon_id) {
      draw_list.AddImage(IconLoader::ToImGuiTexture(entry.icon_id), icon_min,
                         icon_min + ImVec2(icon_size, icon_size), ImVec2(0, 0),
                         ImVec2(1, 1), fg);
    } else {
      IMComponents::Glyph(draw_list, ImVec2(x + tab_pad, y),
                          ImVec2(icon_size, rail_button_), entry.icon, fg,
                          small);
    }
    draw_list.AddText(
        small, small->FontSize,
        ImVec2(icon_min.x + icon_size + icon_gap,
               std::floor(y + (rail_button_ - label_size.y) * 0.5f)),
        fg, entry.window_name);

    x += tab_w + rail_gap_;
  }

  // Divider under strip while the panel is open
  if (!collapsed_)
    draw_list.AddLine(
        ImVec2(strip_min.x, strip_min.y + strip_height_ - 0.5f),
        ImVec2(strip_min.x + width, strip_min.y + strip_height_ - 0.5f),
        EditorColor::panel_stroke, 1.0f);
}

void TabContainer::ApplyCollapse() {
  const float target = collapsed_ ? CollapsedSize() : expanded_width_;
  const float t = 1.0f - std::exp(-14.0f * ImGui::GetIO().DeltaTime);
  current_width_ += (target - current_width_) * t;
  animating_ = std::fabs(target - current_width_) >= 0.5f;
  if (!animating_)
    current_width_ = target;
}
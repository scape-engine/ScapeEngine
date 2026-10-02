#ifndef CONSOLE_PANEL_H
#define CONSOLE_PANEL_H

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

#include "engine/core/logger.h"
/**
 * CONSOLE PANEL
 * Issues:
 * ! - ImGui::TextUnformatted in loop renders all logs each frame, even
 * off-screen ones
 */

namespace Panels {

namespace Detail {

// Apply one row per level in the side column: icon, color, tooltip
struct LevelStyle {
  LogLevel level;
  const char* icon;
  ImU32 color;
  const char* tooltip;
};

inline const LevelStyle kLevels[4] = {
    {LogLevel::Error, ICON_FA_CIRCLE_XMARK, EditorColor::console_error,
     "Errors"},
    {LogLevel::Warning, ICON_FA_TRIANGLE_EXCLAMATION, EditorColor::warning,
     "Warnings"},
    {LogLevel::Info, ICON_FA_MESSAGE, EditorColor::text, "Messages"},
    {LogLevel::Debug, ICON_FA_BUG, EditorColor::text_dim, "Debug"},
};

// Logger formats entries as "[LEVEL] message"; returns index into kLevels
inline uint8_t LevelIndex(const std::string& line) {
  if (line.rfind("[ERROR]", 0) == 0)
    return 0;
  if (line.rfind("[WARNING]", 0) == 0)
    return 1;
  if (line.rfind("[DEBUG]", 0) == 0)
    return 3;
  return 2;  // INFO and anything unrecognized
}

inline bool ContainsNoCase(const std::string& text, const char* needle) {
  if (needle[0] == '\0')
    return true;
  auto it = std::search(text.begin(), text.end(), needle,
                        needle + std::strlen(needle), [](char a, char b) {
                          return std::tolower((unsigned char)a) ==
                                 std::tolower((unsigned char)b);
                        });
  return it != text.end();
}

}  // namespace Detail

inline void ConsolePanel() {
  using namespace Detail;

  static uint64_t last_version = 0;
  static std::deque<std::string> cached_entries;
  static std::vector<uint8_t> cached_levels;  // kLevels index per entry
  static std::vector<int> visible;            // entries passing filters
  static int counts[4] = {};
  static bool show[4] = {true, true, true, true};
  static char filter[128] = "";
  static std::string last_filter;
  static bool auto_scroll = true;
  bool dirty = false;

  // REFRESH CACHE (only when the log changed)
  uint64_t current = Logger::getInstance().GetVersion();
  if (current != last_version) {
    cached_entries = Logger::getInstance().GetLogEntries();
    cached_levels.resize(cached_entries.size());
    std::fill(counts, counts + 4, 0);
    for (size_t i = 0; i < cached_entries.size(); ++i) {
      cached_levels[i] = LevelIndex(cached_entries[i]);
      ++counts[cached_levels[i]];
    }
    last_version = current;
    dirty = true;
  }

  // LAYOUT: [ log | toggles ] over [ filter ]
  const float side_w = 52.0f;
  const float control_h = EditorSizes::control_height + 4.0f;
  const float gap = 6.0f;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float log_w = avail.x - side_w - gap;
  const float log_h = avail.y - control_h - gap;
  ImDrawList& draw_list = *ImGui::GetWindowDrawList();
  ImFont* small = EditorStyles::GetFonts().s;

  // LEVEL TOGGLES (right column)
  for (int i = 0; i < 4; ++i) {
    const ImVec2 min =
        ImVec2(origin.x + log_w + gap, origin.y + i * (control_h + 4.0f));
    const ImVec2 max = ImVec2(min.x + side_w, min.y + control_h);

    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##level", ImVec2(side_w, control_h))) {
      show[i] = !show[i];
      dirty = true;
    }
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
      IMComponents::Tooltip(kLevels[i].tooltip);

    draw_list.AddRectFilled(
        min, max, hovered ? EditorColor::control_hovered : EditorColor::control,
        EditorSizes::control_radius);
    if (show[i])  // active underline
      draw_list.AddLine(ImVec2(min.x + 2.0f, max.y - 1.0f),
                        ImVec2(max.x - 2.0f, max.y - 1.0f),
                        EditorColor::accent_border, 2.0f);

    IMComponents::Glyph(
        draw_list, min, ImVec2(side_w * 0.45f, control_h), kLevels[i].icon,
        show[i] ? kLevels[i].color : EditorColor::text_disabled, small);
    const std::string n = counts[i] > 999 ? "999+" : std::to_string(counts[i]);
    draw_list.AddText(ImVec2(min.x + side_w * 0.5f,
                             min.y + (control_h - ImGui::GetFontSize()) * 0.5f),
                      show[i] ? EditorColor::text : EditorColor::text_disabled,
                      n.c_str());
  }

  // FILTER CHANGED? (buffer is edited by the field drawn below)
  if (last_filter != filter) {
    last_filter = filter;
    dirty = true;
  }

  // REBUILD VISIBLE LIST (only when log, toggles or filter changed)
  if (dirty) {
    visible.clear();
    for (int i = 0; i < static_cast<int>(cached_entries.size()); ++i)
      if (show[cached_levels[i]] && ContainsNoCase(cached_entries[i], filter))
        visible.push_back(i);
  }

  // LOG AREA
  ImGui::SetCursorScreenPos(origin);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorColor::strip);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, EditorSizes::control_radius);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  ImGui::BeginChild("LogScrollingRegion", ImVec2(log_w, log_h),
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  ImGui::PushFont(EditorStyles::GetFonts().console);
  ImDrawList& log_draw = *ImGui::GetWindowDrawList();
  const float line_h = ImGui::GetTextLineHeight();

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(visible.size()));
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const int i = visible[row];
      const LevelStyle& style = kLevels[cached_levels[i]];
      const ImVec2 p = ImGui::GetCursorScreenPos();

      // Dot marks errors and warnings
      if (style.level == LogLevel::Error || style.level == LogLevel::Warning)
        log_draw.AddCircleFilled(ImVec2(p.x + 4.0f, p.y + line_h * 0.5f), 3.0f,
                                 style.color);

      ImGui::SetCursorScreenPos(ImVec2(p.x + 14.0f, p.y));
      ImGui::PushStyleColor(ImGuiCol_Text, style.color);
      ImGui::TextUnformatted(cached_entries[i].c_str());
      ImGui::PopStyleColor();
    }
  }

  // Stick to the bottom while already at the bottom
  if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    ImGui::SetScrollHereY(1.0f);

  ImGui::PopFont();
  ImGui::EndChild();

  // FILTER FIELD
  IMComponents::SearchField(
      draw_list, "##ConsoleFilter", filter, IM_ARRAYSIZE(filter),
      ImVec2(origin.x, origin.y + log_h + gap), ImVec2(log_w, control_h));

  // Register the full extent
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + avail.y));
  ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}  // namespace Panels

#endif  // CONSOLE_PANEL_H
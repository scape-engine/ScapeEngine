#ifndef CONSOLE_PANEL_H
#define CONSOLE_PANEL_H

#include <imgui.h>
#include <deque>
#include <string>
#include <vector>

#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "editor/gui/styles/editor_styles.h"
#include "engine/core/logger.h"

/**
 * CONSOLE PANEL
 * Issues:
 * ! - ImGui::TextUnformatted in loop renders all logs each frame, even
 * off-screen ones
 */

namespace Panels {

inline void ConsolePanel() {
  // Managing its own window with the correct icon
  ImGui::Begin(ICON_FA_TERMINAL " Console");

  // Set background color and corner radius
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(25, 26, 28, 255));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

  ImGui::BeginChild("LogRegion", ImVec2(0, 0), true);
  ImGui::BeginChild("LogScrollingRegion", ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

  ImGui::PushFont(EditorStyles::GetFonts().console);

  static uint64_t last_version = 0;
  static std::deque<std::string> cached_entries;
  static bool auto_scroll = true;

  uint64_t current = Logger::getInstance().GetVersion();
  if (current != last_version) {
    cached_entries = Logger::getInstance().GetLogEntries();
    last_version = current;
  }

  // ImGuiListClipper only renders visible lines
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(cached_entries.size()));

  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
      ImGui::TextUnformatted(cached_entries[i].c_str());
    }
  }

  // Auto-scroll to bottom if at the bottom, or if new logs arrived
  if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
    ImGui::SetScrollHereY(1.0f);
  }

  // Pop after rendering
  ImGui::PopFont();
  ImGui::EndChild();
  ImGui::EndChild();

  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);

  ImGui::End();  // End Console Window
}

}  // namespace Panels

#endif  // CONSOLE_PANEL_H
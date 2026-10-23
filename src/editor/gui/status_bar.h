#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <string>

#include "editor/gui/styles/editor_styles.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"
#include "engine/core/logger.h"
#include "engine/core/version.h"
#include "engine/diagnostics/activity.h"

namespace Panels {

// Includes:
// project name, git status, project status, activity, engine version
inline void StatusBar() {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float height = ImGui::GetFrameHeight();
  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove;

  const bool open = ImGui::BeginViewportSideBar(
      "##SecondaryMenuBar", viewport, ImGuiDir_Down, height, window_flags);
  if (open && ImGui::BeginMenuBar()) {
    const float segment_gap = 24.0f;
    auto gap = [&]() {
      ImGui::Dummy(ImVec2(segment_gap, 0.0f));
    };
    auto colored = [](ImU32 color, const char* text) {
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
    };

    // PROJECT (* when unsaved)
    const bool unsaved = false;  // TODO: project/scene dirty flag
    std::string project = Runtime::Project().ProjectName();
    if (unsaved)
      project += "*";
    ImGui::TextUnformatted(project.c_str());
    gap();

    // GIT (placeholder until git status is wired up)
    colored(EditorColor::text_dim, ICON_FA_CODE_BRANCH " main");
    colored(EditorColor::text_dim, ICON_FA_ARROW_UP " 0");
    colored(EditorColor::text_dim, ICON_FA_ARROW_DOWN " 0");
    colored(EditorColor::text_dim, ICON_FA_PEN " 0");
    gap();

    // CONSOLE COUNTS (dimmed when zero)
    auto count = [&](const char* icon, LogLevel level, ImU32 color) {
      const uint64_t n = Logger::getInstance().GetCount(level);
      char text[32];
      std::snprintf(text, sizeof(text), "%s %llu", icon,
                    static_cast<unsigned long long>(n));
      colored(n > 0 ? color : EditorColor::text_disabled, text);
    };
    count(ICON_FA_CIRCLE_EXCLAMATION, LogLevel::Error,
          EditorColor::console_error);
    count(ICON_FA_TRIANGLE_EXCLAMATION, LogLevel::Warning,
          EditorColor::warning);
    count(ICON_FA_CIRCLE_INFO, LogLevel::Info, EditorColor::accent_border);
    gap();

    // ENGINE ACTIVITY
    const std::string activity = Diagnostics::GetActivity();
    if (!activity.empty())
      colored(EditorColor::accent_border,
              (std::string(ICON_FA_SPINNER " ") + activity).c_str());

    // VERSION (right-aligned)
    char version[32];
    std::snprintf(version, sizeof(version), "v%s", engine_version);
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() -
                         ImGui::CalcTextSize(version).x -
                         ImGui::GetStyle().WindowPadding.x);
    colored(EditorColor::text_dim, version);

    ImGui::EndMenuBar();
  }
  ImGui::End();
}

}  // namespace Panels

#endif  // STATUS_BAR_H
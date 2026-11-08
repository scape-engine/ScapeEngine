#ifndef MENU_BAR_H
#define MENU_BAR_H

#include <imgui.h>
#include <imgui_internal.h>
#include <functional>
#include <string>
#include <vector>
#include "editor/gui/modals/scene_modals.h"
#include "editor/runtime/runtime.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

namespace Panels {

void MenuBar(std::function<void()> onExit, bool& debug_highlight,
             bool& show_metrics, bool& show_log, bool& activate_picker,
             bool& show_style_editor) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  float height = ImGui::GetFrameHeight();
  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
    SceneModals::RequestSave();

  if (ImGui::BeginViewportSideBar("##MainMenuBar", viewport, ImGuiDir_Up,
                                  height, window_flags)) {
    if (ImGui::BeginMenuBar()) {

      // UX Improvement: Added File & Debug icons
      if (ImGui::BeginMenu(ICON_FA_FILE " File")) {
        // Standard UX is to show keyboard shortcuts on the right side
        if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS " New Scene"))
          SceneModals::RunAfterSaveCheck(
              [] { Runtime::Project().NewScene(); });
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save Scene", "Ctrl+S"))
          SceneModals::RequestSave();
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save Scene As..."))
          SceneModals::RequestSaveAs();

        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_FA_CLOCK_ROTATE_LEFT " Recent Projects")) {
          // Copy: opening a project reorders the list
          const std::vector<std::string> recent =
              Runtime::Settings().recent_projects;
          for (const std::string& path : recent) {
            if (ImGui::MenuItem(path.c_str()))
              SceneModals::RunAfterSaveCheck([path] {
                if (Runtime::Project().Load(path))
                  Runtime::Settings().AddRecentProject(path);
              });
          }
          ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_DOOR_OPEN " Exit", "Alt+F4") && onExit)
          onExit();
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu(ICON_FA_BUG " Debug")) {
        ImGui::MenuItem(ICON_FA_HIGHLIGHTER " Highlight ID Conflicts", nullptr,
                        &debug_highlight);
        ImGui::MenuItem(ICON_FA_CHART_BAR " Show Metrics Window", nullptr,
                        &show_metrics);
        ImGui::MenuItem(ICON_FA_ALIGN_LEFT " Show Debug Log Window", nullptr,
                        &show_log);
        ImGui::MenuItem(ICON_FA_PALETTE " Show Style Editor", nullptr,
                        &show_style_editor);
        if (ImGui::MenuItem(ICON_FA_EYE_DROPPER " Activate Picker"))
          activate_picker = true;
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }
    ImGui::End();
  }

  SceneModals::Render();
}

}  // namespace Panels

#endif  // MENU_BAR_H
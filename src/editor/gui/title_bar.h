#ifndef TITLE_BAR_H
#define TITLE_BAR_H

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <string>

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/vendor/IconFontCppHeaders/IconsMaterialDesignIcons.h"
#include "engine/renderer/icons/icon_loader.h"
#include "platform/platform_utils.h"
#include "platform/window_manager.h"

namespace Panels {

// App-drawn title bar: its own row above the menu bar, with the window title
// and window buttons. Only where the platform has no native bar (Windows,
// Linux); macOS keeps its native one
inline void TitleBar(const std::string& title) {
  if (!Platform::DrawsWindowButtons())
    return;

  SDL_Window* window = WindowManager::GetWindow();
  if (Platform::IsBorderlessFullscreen(window)) {
    Platform::SetTitleBarRegion(0.0f, {});  // nothing to drag in fullscreen
    return;
  }

  const float height = 32.0f;  // Windows' standard caption height
  ImGuiViewport* viewport = ImGui::GetMainViewport();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool open = ImGui::BeginViewportSideBar(
      "##TitleBar", viewport, ImGuiDir_Up, height,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoDocking);
  ImGui::PopStyleVar();

  if (open) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();

    // App logo + title (left)
    const float logo_size = 16.0f;
    float x = pos.x + 12.0f;
    const ImVec2 logo_min =
        ImVec2(x, std::floor(pos.y + (height - logo_size) * 0.5f));
    draw_list->AddImage(IconLoader::ToImGuiTexture("logo"), logo_min,
                        ImVec2(logo_min.x + logo_size, logo_min.y + logo_size));
    x += logo_size + 8.0f;
    draw_list->AddText(
        ImVec2(x, std::floor(pos.y + (height - ImGui::GetFontSize()) * 0.5f)),
        EditorColor::text, title.c_str());

    // Window buttons (right)
    const float button_w = 46.0f;  // Windows' standard caption button width
    const float buttons_x = pos.x + width - button_w * 3.0f;

    auto window_button = [&](int index, const char* icon, ImU32 hover_bg) {
      const ImVec2 min = ImVec2(buttons_x + button_w * index, pos.y);
      const ImVec2 max = ImVec2(min.x + button_w, pos.y + height);
      ImGui::SetCursorScreenPos(min);
      ImGui::PushID(index);
      const bool clicked =
          ImGui::InvisibleButton("##window_button", ImVec2(button_w, height));
      ImGui::PopID();
      if (ImGui::IsItemHovered())
        draw_list->AddRectFilled(min, max, hover_bg);
      IMComponents::Glyph(*draw_list, min, ImVec2(button_w, height), icon,
                          EditorColor::text);
      return clicked;
    };

    if (window_button(0, ICON_MDI_WINDOW_MINIMIZE, EditorColor::surface_4))
      Platform::MinimizeWindow(window);
    if (window_button(1,
                      Platform::IsWindowMaximized(window)
                          ? ICON_MDI_WINDOW_RESTORE
                          : ICON_MDI_WINDOW_MAXIMIZE,
                      EditorColor::surface_4))
      Platform::ToggleMaximizeWindow(window);
    if (window_button(2, ICON_MDI_WINDOW_CLOSE, IM_COL32(196, 43, 28, 255)))
      Platform::RequestWindowClose();

    // The whole row drags the window, except the buttons
    Platform::SetTitleBarRegion(
        pos.y + height,
        {Platform::WindowRect{buttons_x, pos.y, button_w * 3.0f, height}});
  }
  ImGui::End();
}

}  // namespace Panels

#endif  // TITLE_BAR_H

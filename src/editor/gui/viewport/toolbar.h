#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <imgui.h>
#include <functional>

#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"

#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

namespace Viewport {
struct ToolbarCallbacks {
  std::function<void()> onStart;
  std::function<void()> onStop;
  std::function<void()> onAddObject;
  std::function<void()> onRemoveObject;
  std::function<void()> onQuit;
  std::function<void()> onSetFullscreen;

  bool fullscreen = false;
};

inline void Toolbar(const ToolbarCallbacks& cb) {
  IMComponents::BeginOverlay();

  ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0, 0, 0, 0));  // Transparent default
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));

  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorSizes::overlay_radius);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
  ImGui::PushFont(EditorStyles::GetFonts().toolbar);

  const float btn_w = ImGui::GetFrameHeight() + 4.0f;

  if (IMComponents::ToolButton(ICON_FA_PLAY, false, ImVec2(btn_w, 0),
                               IM_COL32(90, 200, 90, 255)))
    if (cb.onStart)
      cb.onStart();

  ImGui::SameLine();
  if (IMComponents::ToolButton(ICON_FA_STOP, false, ImVec2(btn_w, 0),
                               IM_COL32(200, 90, 90, 255)))
    if (cb.onStop)
      cb.onStop();

  ImGui::SameLine();
  IMComponents::VerticalDivider();
  ImGui::SameLine();

  if (IMComponents::ToolButton(
          cb.fullscreen ? ICON_FA_COMPRESS : ICON_FA_EXPAND, false,
          ImVec2(btn_w, 0)))
    if (cb.onSetFullscreen)
      cb.onSetFullscreen();
  IMComponents::Tooltip(cb.fullscreen ? "Exit fullscreen (Shift+Space)"
                                      : "Fullscreen viewport (Shift+Space)");

  ImGui::PopFont();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);

  IMComponents::EndOverlay();
}
}  // namespace Viewport

#endif  // TOOLBAR_H
#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <imgui.h>
#include <functional>
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

namespace Panels {

struct ToolbarCallbacks {
  std::function<void()> onStart;
  std::function<void()> onStop;
  std::function<void()> onAddObject;
  std::function<void()> onRemoveObject;
  std::function<void()> onQuit;
};

// Now it just draws buttons, no ImGui::Begin() window wrapper
inline void Toolbar(const ToolbarCallbacks& cb) {
  ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0, 0, 0, 0));  // Transparent default
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
  ImGui::PushFont(EditorStyles::GetFonts().h2);

  // UE5 Green Play
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(90, 200, 90, 255));
  if (ImGui::Button(ICON_FA_PLAY))
    if (cb.onStart)
      cb.onStart();
  ImGui::PopStyleColor();

  ImGui::SameLine();
  // UE5 Red Stop
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 90, 90, 255));
  if (ImGui::Button(ICON_FA_STOP))
    if (cb.onStop)
      cb.onStop();
  ImGui::PopStyleColor();

  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_CUBE))
    if (cb.onAddObject)
      cb.onAddObject();

  ImGui::PopFont();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}
}  // namespace Panels

#endif  // TOOLBAR_H
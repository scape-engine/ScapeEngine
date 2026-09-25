#include "popup_menu.h"
#include <imgui.h>

namespace PopupMenu {

bool Begin(const char* str_id) {
  // Colors are handled by EditorStyles
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

  return ImGui::BeginPopupContextWindow(str_id,
                                        ImGuiPopupFlags_MouseButtonRight);
}

void End() {
  ImGui::EndPopup();
}

void Pop() {
  ImGui::PopStyleVar(3);  // Only pop the 3 vars now
}

bool Item(const char* icon, std::string title) {
  std::string text = std::string(icon) + "   " + title;
  return ImGui::MenuItem(text.c_str());
}

bool ItemLight(std::string title) {
  return ImGui::MenuItem(title.c_str());
}

bool Menu(const char* icon, std::string title) {
  std::string text = std::string(icon) + "   " + title;
  return ImGui::BeginMenu(text.c_str());
}

void EndMenu() {
  ImGui::EndMenu();
}
void Separator() {
  ImGui::Separator();
}

}  // namespace PopupMenu
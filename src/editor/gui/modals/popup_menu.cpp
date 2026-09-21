#include "popup_menu.h"

#include <imgui.h>

#include "editor/editor_ui.h"
#include "editor/gui/styles/editor_styles.h"

namespace PopupMenu {

ImVec4 _background_color = ImVec4(0.2f, 0.2f, 0.25f, 0.9f);
ImVec4 _text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImVec4 _hover_color = ImVec4(0.1f, 0.1f, 0.15f, 1.0f);
ImU32 _outline_color = EditorColor::selection;

bool _open_requested = false;

void _Space() {
  ImGui::Dummy(ImVec2(0.0f, 0.1f));
}

bool Begin() {

  // Custom menu colors
  ImGui::PushStyleColor(ImGuiCol_PopupBg, _background_color);
  ImGui::PushStyleColor(ImGuiCol_Text, _text_color);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, _hover_color);
  ImGui::PushStyleColor(ImGuiCol_Border, _outline_color);

  std::string id = EditorUI::Get()->GenerateIdString();
   if (_open_requested) {
    ImGui::OpenPopup(id.c_str());
    _open_requested = false;
  }
  return ImGui::BeginPopupContextWindow(id.c_str(),
                                        ImGuiPopupFlags_MouseButtonRight);
}

void End() {
  ImGui::EndPopup();
}

void Pop() {
  ImGui::PopStyleColor(4);
}

bool Item(const char* icon, std::string title) {
  _Space();
  std::string text = std::string(icon) + "     " + title;
  return ImGui::MenuItem(text.c_str());
}

bool ItemLight(std::string title) {
  return ImGui::MenuItem(title.c_str());
}

bool Menu(const char* icon, std::string title) {
  _Space();
  std::string text = std::string(icon) + "     " + title;
  return ImGui::BeginMenu(text.c_str());
}

void EndMenu() {
  ImGui::EndMenu();
}

void Separator() {
  _Space();
  ImGui::Separator();
}

void Open() {
  _open_requested = true;
}
}  // namespace PopupMenu
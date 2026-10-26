#include "popup_menu.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "editor/editor_ui.h"
#include "editor/gui/styles/editor_styles.h"

namespace PopupMenu {

bool _open_requested = false;
bool _anchored = false;
ImVec2 _anchor;

bool Begin() {
  // Background and border come from the global popup style; only the hover
  // is solid here (the global one is a faint overlay meant for tree rows)
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                        ImGui::ColorConvertU32ToFloat4(EditorColor::surface_4));
  ImGui::PushStyleColor(ImGuiCol_Border,
                        ImGui::ColorConvertU32ToFloat4(EditorColor::line_subtle));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

  std::string id = EditorUI::Get()->GenerateIdString();
  if (_open_requested) {
    ImGui::OpenPopup(id.c_str());
    if (_anchored)
      ImGui::SetNextWindowPos(_anchor);  // else at the mouse
    _open_requested = false;
  }
  return ImGui::BeginPopupContextWindow(id.c_str(),
                                        ImGuiPopupFlags_MouseButtonRight);
}

void End() {
  ImGui::EndPopup();
}

void Pop() {
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

// Ex variants draw the icon in its own column, aligned across items
bool Item(const char* icon, std::string title) {
  return ImGui::MenuItemEx(title.c_str(), icon);
}

bool ItemLight(std::string title) {
  return ImGui::MenuItem(title.c_str());
}

bool Menu(const char* icon, std::string title) {
  return ImGui::BeginMenuEx(title.c_str(), icon);
}

void EndMenu() {
  ImGui::EndMenu();
}

void Separator() {
  ImGui::Separator();
}

void Open() {
  _open_requested = true;
  _anchored = false;
}

void Open(ImVec2 position) {
  _open_requested = true;
  _anchored = true;
  _anchor = position;
}
}  // namespace PopupMenu

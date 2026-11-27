#include "popup_menu.h"

#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>

#include "editor/editor_ui.h"
#include "editor/gui/styles/editor_styles.h"

namespace PopupMenu {

bool _open_requested = false;
bool _anchored = false;
ImVec2 _anchor;

// ImGui keeps a popup inside the main viewport's work area, and side bars
// (the bottom and right panels) are cut out of that area: a menu opened in
// them would be moved out of them. It stays where it was opened (at its
// anchor, else the mouse), only kept on screen
static ImGuiID _anchored_id = 0;

static void _KeepPlace(ImGuiID id) {
  const ImGuiContext& context = *ImGui::GetCurrentContext();
  for (const ImGuiPopupData& popup : context.OpenPopupStack)
    if (popup.PopupId == id) {
      const ImVec2 at = id == _anchored_id ? _anchor : popup.OpenPopupPos;
      const ImVec2 size = popup.Window ? popup.Window->Size : ImVec2();
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      const ImVec2 min = viewport->Pos;
      const ImVec2 max(min.x + viewport->Size.x - size.x,
                       min.y + viewport->Size.y - size.y);
      ImGui::SetNextWindowPos(ImVec2(std::clamp(at.x, min.x, max.x),
                                     std::clamp(at.y, min.y, max.y)));
      return;
    }
  if (id == _anchored_id)
    _anchored_id = 0;  // closed: a right-click opens it at the mouse
}

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
  const ImGuiID popup_id = ImGui::GetID(id.c_str());
  if (_open_requested) {
    ImGui::OpenPopup(id.c_str());
    _anchored_id = _anchored ? popup_id : 0;  // else at the mouse
    _open_requested = false;
  }
  _KeepPlace(popup_id);
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
  // ImGui overlaps a submenu with its parent by ItemInnerSpacing.x; negative
  // turns it into a gap. Only read while placing the submenu, so it's popped
  // before the submenu's items are drawn
  const ImVec2 inner = ImGui::GetStyle().ItemInnerSpacing;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(-4.0f, inner.y));
  const bool open = ImGui::BeginMenuEx(title.c_str(), icon);
  ImGui::PopStyleVar();
  return open;
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

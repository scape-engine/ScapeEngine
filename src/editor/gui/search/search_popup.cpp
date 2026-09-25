#include "search_popup.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "editor/editor_ui.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/registry/component_registry.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

namespace SearchPopup {

// Set if popup is shown
bool g_show = false;

// Search popup position
ImVec2 g_search_position = ImVec2(0.0f, 0.0f);

// Current search type
std::string g_search_name;

// Set if popup is opened on current frame
bool g_newly_opened = false;

// Search buffer
char g_search_buffer[128] = "";

Entity g_search_entity;

// Returns true if the item was clicked
bool _SearchItem(const std::string& label, const std::string& icon) {
  ImFont* font = EditorStyles::GetFonts().p_bold;
  float font_size = font->FontSize;

  float height = 32.0f;  // UE5 list item height
  ImVec2 position = ImGui::GetCursorScreenPos();
  ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, height);

  ImVec2 p0 = position;
  ImVec2 p1 = position + size;

  // Use invisible button for interaction logic
  ImGui::InvisibleButton(label.c_str(), size);
  bool hovered = ImGui::IsItemHovered();
  bool clicked = hovered && ImGui::IsMouseClicked(0);

  ImDrawList& draw_list = *ImGui::GetWindowDrawList();

  // UE5 Hover State (Blue Pill)
  if (hovered) {
    draw_list.AddRectFilled(p0, p1, IM_COL32(0, 112, 224, 200), 6.0f);
  }

  // Draw Icon
  ImVec2 icon_pos = position + ImVec2(10.0f, (height - font_size) * 0.5f);
  draw_list.AddText(font, font_size, icon_pos, IM_COL32_WHITE, icon.c_str());

  // Draw Text
  float icon_width =
      font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, icon.c_str()).x;
  ImVec2 text_pos = icon_pos + ImVec2(icon_width + 12.0f, 0.0f);
  draw_list.AddText(font, font_size, text_pos, IM_COL32_WHITE, label.c_str());

  // Advance cursor for next item (add small gap)
  ImGui::SetCursorScreenPos(ImVec2(position.x, position.y + height + 2.0f));

  return clicked;
}

void Render() {
  if (!g_show)
    return;

  // Apply UE5 styling to the popup window itself
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));

  // Open the popup ONLY on the first frame it's triggered
  if (g_newly_opened) {
    ImGui::OpenPopup("SearchPopup");
    ImGui::SetNextWindowPos(g_search_position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(350.0f, 400.0f));
  }

  // Standard Popup (closes automatically if user clicks outside!)
  if (ImGui::BeginPopup("SearchPopup",
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {

    // Title
    std::string title = "Add " + g_search_name;
    IMComponents::Label(title, EditorStyles::GetFonts().h3_bold);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    // Search bar styling
    ImGui::PushFont(EditorStyles::GetFonts().h3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

    if (g_newly_opened) {
      ImGui::SetKeyboardFocusHere();
      g_search_buffer[0] = '\0';  // Clear buffer on open
    }

    ImGui::InputTextWithHint("##SearchInput",
                             ICON_FA_MAGNIFYING_GLASS " Search...",
                             g_search_buffer, IM_ARRAYSIZE(g_search_buffer));

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopItemWidth();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    // Get filter from search buffer
    std::string filter(g_search_buffer);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    // Search results container
    IMComponents::BeginClippedChild(ImGui::GetContentRegionAvail());
    {
      for (const auto& [name, component] : ComponentRegistry::Get()) {
        // Skip if entity already has this component
        if (component.has && component.has(g_search_entity))
          continue;

        // Filter by search text
        if (!filter.empty()) {
          std::string name_lower = component.name;
          std::transform(name_lower.begin(), name_lower.end(),
                         name_lower.begin(), ::tolower);
          if (name_lower.find(filter) == std::string::npos)
            continue;
        }

        // Draw item and handle click!
        if (_SearchItem(component.name, component.icon)) {
          // Check your exact ComponentRegistry API, usually it's "add" or
          // "emplace"
          if (component.add) {
            component.add(g_search_entity);
          }
          Close();  // Close popup after adding component
        }
      }
    }
    IMComponents::EndClippedChild();

    g_newly_opened = false;
    ImGui::EndPopup();
  } else {
    // If BeginPopup returns false, it means the user clicked away or pressed
    // ESC. We clean up our state so it can be opened again later.
    g_show = false;
  }

  // Pop global styles
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

void Close() {
  g_show = false;
  ImGui::CloseCurrentPopup();
}

void SearchComponents(ImVec2 position, Entity target) {
  g_show = true;
  g_search_position = position;
  g_search_name = "Component";
  g_newly_opened = true;
  g_search_entity = target;
}

}  // namespace SearchPopup
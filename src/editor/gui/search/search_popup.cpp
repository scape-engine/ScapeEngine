#include "search_popup.h"

#include <cstdint>
#include <string>

#include "editor/editor_ui.h"
#include "editor/gui/components/im_components.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/registry/component_registry.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

// TODO: replace icon code with IconPool

namespace SearchPopup {

// Set if popup is shown
bool g_show = false;

// Search popup position
ImVec2 g_search_position = ImVec2(0.0f, 0.0f);

// Current search type
std::string g_search_name;

// Set if popup is opened on current frame
bool g_newly_opened;

// Search buffer
char g_search_buffer[128];

Entity g_search_entity;

void _SearchItem(std::string label, std::string icon) {

  // EVALUATE
  ImFont* font = EditorStyles::GetFonts().p_bold;
  float font_size = font->FontSize;
  ImVec2 text_size =
      font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str());

  float y_padding = 10.0f;
  ImVec2 position = ImGui::GetCursorScreenPos();
  ImVec2 size =
      ImVec2(ImGui::GetContentRegionAvail().x, text_size.y + y_padding * 2.0f);

  ImVec2 p0 = position;
  ImVec2 p1 = position + size;

  bool hovered = ImGui::IsMouseHoveringRect(p0, p1);
  bool clicked = hovered && ImGui::IsMouseClicked(0);

  ImDrawList& draw_list = *ImGui::GetWindowDrawList();

  // DRAW BACKGROUND
  ImU32 color = hovered ? IM_COL32(50, 50, 180, 70) : IM_COL32(50, 50, 68, 50);
  draw_list.AddRectFilled(p0, p1, color, 10.0f);

  // ! DRAW ICON (placeholder)
  ImVec2 icon_position = position + ImVec2(15.0f, y_padding);
  draw_list.AddText(font, font_size, icon_position, IM_COL32_WHITE,
                    icon.c_str());

  // DRAW TEXT
  float icon_width =
      font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, icon.c_str()).x;
  ImVec2 text_position = icon_position + ImVec2(icon_width + 12.0f, 0.0f);
  draw_list.AddText(font, font_size, text_position, IM_COL32_WHITE,
                    label.c_str());

  // ADVANCE CURSOR
  ImGui::Dummy(ImVec2(0.0f, size.y - 2.0f));

  // TODO: HANDLE CLICK ACTION
}

void Render() {
  // Check for opened popup
  if (g_show) {
    ImGui::OpenPopup("Search");
    ImGui::SetNextWindowPos(g_search_position, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(400.0f, 350.0f));
  }

  // Draw popup modal
  if (ImGui::BeginPopupModal("Search", nullptr,
                             ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize)) {
    // Title
    std::string title = "Search " + g_search_name;
    IMComponents::Label(title, EditorStyles::GetFonts().h3_bold);
    ImGui::Dummy(ImVec2(0.0f, 1.0f));

    // Hint
    IMComponents::Label("Press ESC to cancel, click to select.",
                        EditorStyles::GetFonts().s);
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    // Search bar
    ImGui::PushFont(EditorStyles::GetFonts().h3);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

    if (g_newly_opened) {
      ImGui::SetKeyboardFocusHere();
      g_search_buffer[0] = '\0';  // Clear buffer on open
    }

    ImGui::InputTextWithHint("##SearchInput", "Search...", g_search_buffer,
                             IM_ARRAYSIZE(g_search_buffer),
                             ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::PopFont();
    ImGui::PopItemWidth();
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    // Get filter from search buffer
    std::string filter(g_search_buffer);

    // Search results
    uint32_t n_search_results = 10;

    IMComponents::BeginClippedChild("SearchResults", ImGui::GetContentRegionAvail());
    {
      for (const auto& [name, component] : ComponentRegistry::Get()) {

        // Skip if entity already has this component
        if (component.has && component.has(g_search_entity))
          continue;

        // Filter by search text
        if (!filter.empty()) {
          std::string name_lower = component.name;
          std::string filter_lower = filter;
          // Simple case-insensitive contains check
          std::transform(name_lower.begin(), name_lower.end(),
                         name_lower.begin(), ::tolower);
          std::transform(filter_lower.begin(), filter_lower.end(),
                         filter_lower.begin(), ::tolower);
          if (name_lower.find(filter_lower) == std::string::npos)
            continue;
        }

        // Draw item
        _SearchItem(component.name, component.icon);
      }
    }
    IMComponents::EndClippedChild();

    // Check for closing
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      Close();
    }

    g_newly_opened = false;
    ImGui::EndPopup();
  }
}

void Close() {
  g_show = false;
  ImGui::CloseCurrentPopup();
}

void SearchComponents(ImVec2 position, Entity target) {
  g_show = true;
  g_search_position = position;
  g_search_name = "Components";
  g_newly_opened = true;
  g_search_entity = target;
}

}  // namespace SearchPopup
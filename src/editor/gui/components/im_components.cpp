#include "im_components.h"

#include <imgui_internal.h>
#include <implot.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "editor/editor_ui.h"
#include "editor/gui/styles/editor_styles.h"
#include "editor/gui/utils/gui_utils.h"
#include "editor/vendor/IconFontCppHeaders/IconsFontAwesome6.h"

#include "engine/core/logger.h"

namespace IMComponents {

//=============================================================================
// FORMATTING FUNCTIONS
//=============================================================================
std::string _Format_Num(int32_t number) {
  std::string num_str = std::to_string(number);
  int32_t insert_position = num_str.length() - 3;

  while (insert_position > 0) {
    num_str.insert(insert_position, ",");
    insert_position -= 3;
  }

  return num_str;
}

std::string _Format_Num(uint32_t number) {
  std::string num_str = std::to_string(number);
  int32_t insert_position = num_str.length() - 3;

  while (insert_position > 0) {
    num_str.insert(insert_position, ",");
    insert_position -= 3;
  }

  return num_str;
}

std::string _Format_Num(float value, uint8_t precision = 2) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(precision) << value;
  std::string result = oss.str();

  size_t decimal_pos = result.find('.');
  std::string integer_part = result.substr(0, decimal_pos);
  std::string fractional_part =
      (decimal_pos != std::string::npos) ? result.substr(decimal_pos) : "";

  int insert_pos = integer_part.length() - 3;
  while (insert_pos > 0) {
    integer_part.insert(insert_pos, ",");
    insert_pos -= 3;
  }

  return integer_part + fractional_part;
}

//=============================================================================
// HEADLINE
//=============================================================================
void Headline(std::string title, const char* icon, bool separator) {
  const float margin_top = 20.0f - ImGui::GetStyle().WindowPadding.y;
  const float margin_bottom = 3.0f;
  const float margin_sub_separator = 7.0f;

  ImGui::Dummy(ImVec2(0.0f, margin_top));
  {
    // ! Check if we actually need the if statement
    if (icon && icon[0] != '\0') {
      ImGui::Text("%s", icon);
      ImGui::SameLine();
    }
    ImGui::PushFont(EditorStyles::GetFonts().h3_bold);
    ImGui::Text("%s", title.c_str());
    ImGui::PopFont();
  }
  ImGui::Dummy(ImVec2(0.0f, margin_bottom));

  if (separator) {
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, margin_sub_separator));
  }
}

//=============================================================================
// TOOLTIP
//=============================================================================
void Tooltip(std::string tooltip) {
  if (!tooltip.empty() && ImGui::IsItemHovered()) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.8f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));

    ImGui::SetTooltip("%s", tooltip.c_str());

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
  }
}

//=============================================================================
// TOGGLE BUTTON
//=============================================================================
void ToggleButton(ImDrawList& draw_list, std::string text, bool& value,
                  std::string tooltip, ImVec2 position) {
  static const ImVec2 padding = ImVec2(8.0f, 8.0f);
  static const float rounding = 5.0f;
  ImFont* font = ImGui::GetFont();

  ImVec2 text_size =
      font->CalcTextSizeA(font->FontSize, FLT_MAX, -1.0f, text.c_str());
  ImVec2 button_size = text_size + padding * 2;
  ImVec2 p0 = position;
  ImVec2 p1 = position + button_size;

  bool hovered = ImGui::IsMouseHoveringRect(p0, p1);
  bool clicked = hovered && ImGui::IsMouseClicked(0);
  if (clicked)
    value = !value;

  // Draw background
  ImU32 bg_color =
      value ? EditorColor::element_active : IM_COL32(0x40, 0x45, 0x4D, 0xFF);
  draw_list.AddRectFilled(p0, p1, bg_color, rounding);

  // Draw text
  draw_list.AddText(font, font->FontSize, p0 + padding, EditorColor::text,
                    text.c_str());
}

//=============================================================================
// BUTTONS
//=============================================================================
bool ButtonBig(std::string label, std::string _tooltip) {
  bool pressed = false;

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        GUIUtils::Darken(EditorColor::element_active, 0.3f));

  pressed = ImGui::Button(label.c_str());

  Tooltip(_tooltip);

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  return pressed;
}

bool ButtonBig(std::string label, ImU32 color, std::string _tooltip) {
  bool pressed = false;

  ImGui::PushStyleColor(ImGuiCol_Button, color);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        GUIUtils::Lighten(color, 0.35f));

  pressed = ButtonBig(label, _tooltip);

  ImGui::PopStyleColor(2);

  return pressed;
}

//=============================================================================
// LABELS
//=============================================================================
void Label(std::string text) {
  ImGui::Text("%s", text.c_str());
}

void Label(std::string text, ImU32 color) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::Text("%s", text.c_str());
  ImGui::PopStyleColor();
}

void Label(std::string text, ImFont* font) {
  ImGui::PushFont(font);
  ImGui::Text("%s", text.c_str());
  ImGui::PopFont();
}

void Label(std::string text, ImFont* font, ImU32 color) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::PushFont(font);
  ImGui::Text("%s", text.c_str());
  ImGui::PopFont();
  ImGui::PopStyleColor();
}

void FlagLabel(std::string text, bool flag, bool bold) {
  ImFont* font = ImGui::GetFont();
  IMComponents::Label(text + ": ", font);
  ImGui::SameLine();
  IMComponents::Label(flag ? "Yes" : "No", flag ? IM_COL32(145, 255, 145, 200)
                                                : IM_COL32(255, 145, 145, 230));
}

void VectorLabel(std::string text, const glm::vec3& vector, bool bold) {
  ImFont* font = ImGui::GetFont();
  IMComponents::Label(text + ": ", font);
  ImGui::SameLine();
  IMComponents::Label(_Format_Num(vector.x) + "x ",
                      IM_COL32(255, 145, 145, 230));
  ImGui::SameLine();
  IMComponents::Label(_Format_Num(vector.y) + "y ",
                      IM_COL32(145, 255, 145, 230));
  ImGui::SameLine();
  IMComponents::Label(_Format_Num(vector.z) + "z ",
                      IM_COL32(145, 145, 255, 230));
}

//=============================================================================
// ICONS
//=============================================================================
void TryIcon(const char* icon, float y_padding) {
  if (icon && icon[0] != '\0') {
    float current_y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(current_y + y_padding);
    ImGui::Text("%s", icon);
    ImGui::SetCursorPosY(current_y);
  }
}

void TryIcon(const char* icon, ImU32 color, float y_padding) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  TryIcon(icon, y_padding);
  ImGui::PopStyleColor();
}

//=============================================================================
// INPUT WIDGETS
//=============================================================================
void Input(std::string label, bool& value) {
  label += ":";
  ImGui::PushID(EditorUI::Get()->GenerateId());
  if (ImGui::BeginTable("##table", 2,
                        ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_NoBordersInBody |
                            ImGuiTableFlags_NoSavedSettings)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label.c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::PushItemWidth(-1);
    std::string id = EditorUI::Get()->GenerateIdString();
    ImGui::Checkbox(id.c_str(), &value);
    ImGui::PopItemWidth();

    ImGui::EndTable();
  }
  ImGui::PopID();
}

void Input(std::string label, int32_t& value, float speed) {
  label += ":";
  ImGui::PushID(EditorUI::Get()->GenerateId());
  if (ImGui::BeginTable("##table", 2,
                        ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_NoBordersInBody |
                            ImGuiTableFlags_NoSavedSettings)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label.c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::PushItemWidth(-1);
    std::string id = EditorUI::Get()->GenerateIdString();
    ImGui::DragInt(id.c_str(), &value, speed, 0, 0, "%d");
    ImGui::PopItemWidth();

    ImGui::EndTable();
  }
  ImGui::PopID();
}

void Input(std::string label, float& value, float speed) {
  label += ":";
  ImGui::PushID(EditorUI::Get()->GenerateId());
  if (ImGui::BeginTable("##table", 2,
                        ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_NoBordersInBody |
                            ImGuiTableFlags_NoSavedSettings)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label.c_str());

    ImGui::TableSetColumnIndex(1);
    ImGui::PushItemWidth(-1);
    std::string id = EditorUI::Get()->GenerateIdString();
    ImGui::DragFloat(id.c_str(), &value, speed, 0.0f, 0.0f, "%.2f");
    ImGui::PopItemWidth();

    ImGui::EndTable();
  }
  ImGui::PopID();
}

void Input(std::string label, glm::vec3& value, float speed) {
  label += ":";

  // EVALUATE
  const float item_spacing_x = 6.0f;
  float x_region_avail = ImGui::GetContentRegionAvail().x;
  float label_width = x_region_avail * 0.3f;

  // Width of one axis button ("X"/"Y"/"Z") with current frame padding
  float button_width =
      ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f;

  // Space for the three drag fields = second column minus 3 buttons minus 5
  // gaps (button<->drag ×3, group<->group ×2)
  float components_width = (x_region_avail - label_width) -
                           3.0f * button_width - 5.0f * item_spacing_x -
                           ImGui::GetStyle().ColumnsMinSpacing;
  components_width = ImMax(components_width, 3.0f * 24.0f);

  ImGui::PushID(EditorUI::Get()->GenerateId());

  // SETUP COLUMNS
  ImGui::Columns(2);
  ImGui::SetColumnWidth(0, label_width);

  // LABEL
  ImGui::Text("%s", label.c_str());
  ImGui::NextColumn();

  // SETUP COMPONENTS
  ImGui::PushMultiItemsWidths(3, components_width);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0f, 0.0f});

  // X COMPONENT
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.0f, 0.25f, 0.3f});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4{0.8f, 0.0f, 0.25f, 0.4f});
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.0f, 0.25f, 0.7f});
  if (ImGui::Button("X"))
    value.x = 0.0f;
  ImGui::PopStyleColor(3);
  ImGui::SameLine();
  ImGui::DragFloat("##_X", &value.x, speed, 0.0f, 0.0f, "%.2f");
  ImGui::PopItemWidth();
  ImGui::SameLine();

  // Y COMPONENT
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.25f, 0.8f, 0.25f, 0.3f});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4{0.25f, 0.8f, 0.25f, 0.4f});
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4{0.25f, 0.8f, 0.25f, 0.7f});
  if (ImGui::Button("Y"))
    value.y = 0.0f;
  ImGui::PopStyleColor(3);
  ImGui::SameLine();
  ImGui::DragFloat("##_Y", &value.y, speed, 0.0f, 0.0f, "%.2f");
  ImGui::PopItemWidth();
  ImGui::SameLine();

  // Z COMPONENT
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 0.3f});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4{0.1f, 0.25f, 0.8f, 0.4f});
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.25f, 0.8f, 0.7f});
  if (ImGui::Button("Z"))
    value.z = 0.0f;

  ImGui::PopStyleColor(3);
  ImGui::SameLine();
  ImGui::DragFloat("##_Z", &value.z, speed, 0.0f, 0.0f, "%.2f");
  ImGui::PopItemWidth();

  // EXIT
  ImGui::PopStyleVar();
  ImGui::Columns(1);
  ImGui::PopID();

  // PADDING
  ImGui::Dummy(ImVec2(0.0f, 2.0f));
}

//=============================================================================
// INDICATOR LABELS
//=============================================================================
void IndicatorLabel(std::string label, std::string value,
                    std::string additional) {
  ImGui::Text("%s", label.c_str());
  ImGui::SameLine();
  ImGui::Text("%s", value.c_str());
  if (!additional.empty()) {
    ImGui::SameLine();
    ImGui::Text("%s", additional.c_str());
  }
}

void IndicatorLabel(std::string label, int32_t value, std::string additional) {
  std::string text = _Format_Num(value);
  IndicatorLabel(label, text, additional);
}

void IndicatorLabel(std::string label, uint32_t value, std::string additional) {
  std::string text = _Format_Num(value);
  IndicatorLabel(label, text, additional);
}

void IndicatorLabel(std::string label, float value, std::string additional) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.0f", value);
  IndicatorLabel(label, buffer, additional);
}

void IndicatorLabel(std::string label, double value, std::string additional) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.0f", value);
  IndicatorLabel(label, buffer, additional);
}

//=============================================================================
// EXTENDABLE SETTINGS
//=============================================================================
bool ExtendableSettings(std::string label, bool& value, const char* icon) {
  ImVec2 cursor = ImGui::GetCursorPos();
  std::string id = EditorUI::Get()->GenerateIdString();
  ImGui::Checkbox(id.c_str(), &value);

  cursor.x += 35.0f;
  ImGui::SetCursorPos(cursor);
  if (icon && icon[0] != '\0') {
    ImGui::Text("%s", icon);
  }

  cursor.x += 45.0f;
  ImGui::SetCursorPos(cursor);
  bool extended = ImGui::CollapsingHeader(label.c_str());

  if (extended)
    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 16.0f));

  return extended;
}

//=============================================================================
// HEADER
//=============================================================================
bool Header(std::string label) {
  return ImGui::CollapsingHeader(label.c_str());
}

//=============================================================================
// COLOR PICKER
//=============================================================================
void ColorPicker(std::string label, glm::vec3& value) {
  static glm::vec3* opened_for = nullptr;
  static glm::vec3 initial_color;

  label += ":";
  ImU32 color = IM_COL32(static_cast<unsigned char>(value.r * 255.0f),
                         static_cast<unsigned char>(value.g * 255.0f),
                         static_cast<unsigned char>(value.b * 255.0f), 255);

  ImGui::PushID(EditorUI::Get()->GenerateId());
  if (ImGui::BeginTable("##table", 2,
                        ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_NoBordersInBody |
                            ImGuiTableFlags_NoSavedSettings)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.7f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label.c_str());

    ImGui::TableSetColumnIndex(1);
    if (ImGui::Button("Pick Color")) {
      opened_for = &value;
      initial_color = value;
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
    std::string id = EditorUI::Get()->GenerateIdString();
    ImGui::Button(id.c_str(), ImVec2(-1.0f, 0.0f));
    ImGui::PopStyleColor(3);

    ImGui::EndTable();
  }
  ImGui::PopID();

  if (opened_for == &value) {
    ImGui::OpenPopup("Color Picker");
    ImGui::SetNextWindowPos(ImGui::GetCursorScreenPos());
  }

  if (ImGui::BeginPopup("Color Picker", ImGuiWindowFlags_NoMove)) {
    ImGui::Text("%s", ICON_FA_PALETTE);
    ImGui::SameLine();
    ImGui::Text(" Pick Color");

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    std::string id = EditorUI::Get()->GenerateIdString();
    float values[3] = {value.r, value.g, value.b};
    ImGui::PushItemWidth(140);
    ImGui::ColorPicker3(id.c_str(), values, ImGuiColorEditFlags_NoSidePreview);
    ImGui::PopItemWidth();
    value = glm::vec3(values[0], values[1], values[2]);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (ButtonBig(ICON_FA_XMARK " Cancel")) {
      ImGui::CloseCurrentPopup();
      opened_for = nullptr;
      value = initial_color;
    }
    ImGui::SameLine();
    if (ButtonBig(ICON_FA_CHECK " Apply")) {
      ImGui::CloseCurrentPopup();
      opened_for = nullptr;
    }

    ImGui::EndPopup();
  }
}

//=============================================================================
// SPARKLINE GRAPH
//=============================================================================
void SparklineGraph(const char* id, const float* values, int32_t count,
                    float min_v, float max_v, int32_t offset,
                    const ImVec4& color, const ImVec2& size) {
  ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
  if (ImPlot::BeginPlot(id, size, ImPlotFlags_CanvasOnly)) {
    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations,
                      ImPlotAxisFlags_NoDecorations);
    ImPlot::SetupAxesLimits(0, count - 1, min_v, max_v, ImGuiCond_Always);
    ImPlot::SetNextLineStyle(color);
    ImPlot::SetNextFillStyle(color, 0.25);
    ImPlot::PlotLine(id, values, count, 1, 0, ImPlotLineFlags_Shaded, offset);
    ImPlot::EndPlot();
  }
  ImPlot::PopStyleVar();
}

//=============================================================================
// CLIPPED CHILD
//=============================================================================
void BeginClippedChild(const char* id, ImVec2 size, ImVec2 position) {
  ImGui::SetCursorScreenPos(position);
  ImGui::PushClipRect(position, position + size, true);
  ImGui::BeginChild(id, size);
}

void EndClippedChild() {
  ImGui::EndChild();
  ImGui::PopClipRect();
}

//=============================================================================
// CARET (EXPANSION ARROW)
//=============================================================================
bool Caret(bool& opened, ImDrawList& draw_list, ImVec2 position, ImVec2 offset,
           ImU32 color, ImU32 hovered_color) {
  float circle_radius = 9.0f;
  ImVec2 circle_position = ImVec2(position.x + circle_radius + offset.x,
                                  position.y + circle_radius * 0.5f + offset.y);
  const ImVec2 mouse_position = ImGui::GetMousePos();

  float circle_distance = (mouse_position.x - circle_position.x) *
                              (mouse_position.x - circle_position.x) +
                          (mouse_position.y - circle_position.y) *
                              (mouse_position.y - circle_position.y);

  bool circle_hovered = circle_distance <= (circle_radius * circle_radius);
  bool circle_clicked = ImGui::IsMouseClicked(0) && circle_hovered;

  ImU32 circle_color = circle_hovered ? hovered_color : color;

  draw_list.AddCircleFilled(circle_position, circle_radius, circle_color);

  const char* icon = opened ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT;
  draw_list.AddText(position, EditorColor::text, icon);

  if (circle_clicked)
    opened = !opened;

  return circle_clicked;
}

//=============================================================================
// ICON BUTTON
//=============================================================================
bool IconButton(const char* icon, ImDrawList& draw_list, ImVec2 position,
                ImVec2 offset, ImU32 color, ImU32 hovered_color) {
  float circle_radius = 9.0f;
  ImVec2 circle_position = ImVec2(position.x + circle_radius + offset.x,
                                  position.y + circle_radius * 0.5f + offset.y);
  const ImVec2 mouse_position = ImGui::GetMousePos();

  float circle_distance = (mouse_position.x - circle_position.x) *
                              (mouse_position.x - circle_position.x) +
                          (mouse_position.y - circle_position.y) *
                              (mouse_position.y - circle_position.y);

  bool circle_hovered = circle_distance <= (circle_radius * circle_radius);
  bool circle_clicked = ImGui::IsMouseClicked(0) && circle_hovered;

  ImU32 circle_color = circle_hovered ? hovered_color : color;

  draw_list.AddCircleFilled(circle_position, circle_radius, circle_color);
  draw_list.AddText(position, EditorColor::text, icon);

  return circle_clicked;
}

//=============================================================================
// LOADING BUFFER (SPINNER)
//=============================================================================
void LoadingBuffer(ImDrawList& draw_list, ImVec2 position, float radius,
                   int thickness, const ImU32& color) {
  ImGuiContext& context = *ImGui::GetCurrentContext();

  ImVec2 size(radius * 2, radius * 2);
  const ImRect bb(position, ImVec2(position.x + size.x, position.y + size.y));
  ImGui::ItemSize(bb);
  ImGui::ItemAdd(bb, EditorUI::Get()->GenerateId());

  draw_list.PathClear();

  int n_segments = 30;
  const int start = abs(ImSin(context.Time * 1.8f) * (n_segments - 5));
  const float a_min = IM_PI * 2.0f * ((float)start) / (float)n_segments;
  const float a_max =
      IM_PI * 2.0f * ((float)n_segments - 3) / (float)n_segments;
  const ImVec2 center = ImVec2(position.x + radius, position.y + radius);

  for (int i = 0; i < n_segments; i++) {
    const float a = a_min + ((float)i / (float)n_segments) * (a_max - a_min);
    draw_list.PathLineTo(
        ImVec2(center.x + ImCos(a + context.Time * 8) * radius,
               center.y + ImSin(a + context.Time * 8) * radius));
  }

  draw_list.PathStroke(color, false, thickness);
}

}  // namespace IMComponents
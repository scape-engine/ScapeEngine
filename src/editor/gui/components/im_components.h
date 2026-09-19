#ifndef IM_COMPONENTS_H
#define IM_COMPONENTS_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <functional>
#include <glm/glm.hpp>
#include <string>

namespace IMComponents {

void Headline(std::string title, const char* icon = "", bool separator = true);

void Tooltip(std::string tooltip);

void ToggleButton(ImDrawList& draw_list, std::string text, bool& value,
                  std::string tooltip, ImVec2 position);

bool ButtonBig(std::string label, std::string tooltip = "");
bool ButtonBig(std::string label, ImU32 color, std::string tooltip = "");

void Label(std::string text);
void Label(std::string text, ImU32 color);
void Label(std::string text, ImFont* font);
void Label(std::string text, ImFont* font, ImU32 color);

void FlagLabel(std::string text, bool flag, bool bold = false);
void VectorLabel(std::string text, const glm::vec3& vector, bool bold = false);

void TryIcon(const char* icon, float y_padding = 2.5f);
void TryIcon(const char* icon, ImU32 color, float y_padding = 2.5f);

void Input(std::string label, bool& value);
void Input(std::string label, int32_t& value, float speed = 0.1f);
void Input(std::string label, float& value, float speed = 0.1f);
void Input(std::string label, glm::vec3& value, float speed = 0.1f);

void IndicatorLabel(std::string label, std::string value,
                    std::string additional = "");
void IndicatorLabel(std::string label, int32_t value,
                    std::string additional = "");
void IndicatorLabel(std::string label, uint32_t value,
                    std::string additional = "");
void IndicatorLabel(std::string label, float value,
                    std::string additional = "");
void IndicatorLabel(std::string label, double value,
                    std::string additional = "");

bool ExtendableSettings(std::string label, bool& value, const char* icon = "");

bool Header(std::string label);

void ColorPicker(std::string label, glm::vec3& value);
void SparklineGraph(const char* id, const float* values, int32_t count,
                    float min_v, float max_v, int32_t offset,
                    const ImVec4& color, const ImVec2& size);

// Begin a child and push fitting clip rect
void BeginClippedChild(const char* id, ImVec2 size,
                       ImVec2 position = ImGui::GetCursorScreenPos());

// End child and pop clip rect
void EndClippedChild();

// Draw clickable expansion caret at given cursor screen position
// return true if clicked
bool Caret(bool& opened, ImDrawList& draw_list, ImVec2 position, ImVec2 offset,
           ImU32 color, ImU32 hovered_color);

// Draw clickable icon button at given cursor screen position
// return true if clicked
bool IconButton(const char* icon, ImDrawList& draw_list, ImVec2 position,
                ImVec2 offset = ImVec2(-1.0f, 2.0f),
                ImU32 color = IM_COL32(0, 0, 0, 0),
                ImU32 hovered_color = IM_COL32(65, 65, 80, 120));

// Draw loading buffer at current cursor position
void LoadingBuffer(ImDrawList& draw_list, ImVec2 position, float radius,
                   int thickness, const ImU32& color);

}  // namespace IMComponents

#endif  // IM_COMPONENTS_H
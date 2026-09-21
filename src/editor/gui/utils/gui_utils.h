#ifndef GUI_UTILS_H
#define GUI_UTILS_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <algorithm>
#include <glm/glm.hpp>
#include <string>

// TODO: tie cursor logic with /platform/input/cursor

namespace GUIUtils {

// Lighten color by given amount
ImU32 Lighten(ImU32 color, float amount);

// Darken color by given amount
ImU32 Darken(ImU32 color, float amount);

// Lerp between given colors
ImVec4 LerpColors(const ImVec4& a, const ImVec4& b, float t);

std::string FormatBytes(uint64_t bytes);

// Return relative scroll value of current UI child (0.0 to 1.0)
float GetChildScrollValue();

// Calculate size and offset for aspect fitting
void CalculateAspectFitting(float aspectRatio, ImVec2& size, ImVec2& offset);

glm::vec2 KeepCursorInBounds(glm::vec4 bounds, bool& cursorMoved, float offset);

// quadrant: 0 = top-left, 1 = top-right, 2 = bottom-right, 3 = bottom-left
void FillCorner(ImDrawList* draw_list, ImVec2 corner, float radius,
                    int quadrant, ImU32 color);

void HideDockTabBar();

// Submit a dockspace whose node never shows a tab bar and can't be split
void HiddenTabDockSpace(ImGuiID id, ImVec2 size);

// Dockspace node that never shows tabs, can't be split, and refuses drops
void HostDockSpace(ImGuiID id, ImVec2 size);

// Force the window begun next into the given dock node
void DockNextWindowInto(ImGuiID dock_id);

// Return title for window with adjusted spacing
std::string WindowTitle(const char* title);

// Return if the current window is focused
bool WindowFocused();

// Return if current window is hovered
bool WindowHovered();

}  // namespace GUIUtils

#endif  // GUI_UTILS_H
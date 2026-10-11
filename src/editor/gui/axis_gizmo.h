#ifndef AXIS_GIZMO_H
#define AXIS_GIZMO_H

#include <imgui.h>
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "editor/camera/god_camera.h"
#include "editor/gui/styles/editor_styles.h"
#include "engine/transform/transform.h"

namespace Panels {

// Blender-style navigation gizmo: shows world axes as seen by the camera,
// click a ball to look along that axis
inline void ViewAxisGizmo(ImVec2 center, float radius, const glm::mat4& view,
                          TransformComponent& camera) {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const glm::mat3 rotation = glm::mat3(view);  // world -> camera
  const float ball_r = std::floor(radius * 0.24f);

  auto with_alpha = [](ImU32 c, int a) {
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
  };

  struct Axis {
    glm::vec3 dir;
    ImU32 color;
    const char* label;  // nullptr for negative axes
    glm::vec3 cam;      // direction in camera space
    ImVec2 screen;
  };
  Axis axes[6] = {
      {{1, 0, 0}, EditorColor::axis_x, "X"},
      {{0, 1, 0}, EditorColor::axis_y, "Y"},
      {{0, 0, 1}, EditorColor::axis_z, "Z"},
      {{-1, 0, 0}, EditorColor::axis_x, nullptr},
      {{0, -1, 0}, EditorColor::axis_y, nullptr},
      {{0, 0, -1}, EditorColor::axis_z, nullptr},
  };

  for (Axis& a : axes) {
    a.cam = rotation * a.dir;
    // Camera space is y-up, screen space is y-down
    a.screen = ImVec2(center.x + a.cam.x * radius, center.y - a.cam.y * radius);
  }

  // Painter's order: farthest first (left-handed: +z points away from viewer)
  std::sort(std::begin(axes), std::end(axes),
            [](const Axis& l, const Axis& r) { return l.cam.z > r.cam.z; });

  // Faint backdrop on hover so the widget reads as interactive
  const float extent = radius + ball_r;
  const bool hovered =
      ImGui::IsMouseHoveringRect(ImVec2(center.x - extent, center.y - extent),
                                 ImVec2(center.x + extent, center.y + extent));
  if (hovered)
    draw_list->AddCircleFilled(center, extent + 2.0f, EditorColor::overlay_bg);

  ImFont* small = EditorStyles::GetFonts().s;
  for (const Axis& a : axes) {
    if (a.label) {
      // Positive axis: line from center, filled ball with label
      draw_list->AddLine(center, a.screen, a.color, 2.0f);
      draw_list->AddCircleFilled(a.screen, ball_r, a.color);
      const ImVec2 ts =
          small->CalcTextSizeA(small->FontSize, FLT_MAX, 0.0f, a.label);
      draw_list->AddText(
          small, small->FontSize,
          ImVec2(a.screen.x - ts.x * 0.5f, a.screen.y - ts.y * 0.5f),
          EditorColor::void_bg, a.label);
    } else {
      // Negative axis: translucent fill with a ring
      draw_list->AddCircleFilled(a.screen, ball_r, with_alpha(a.color, 60));
      draw_list->AddCircle(a.screen, ball_r, with_alpha(a.color, 160), 0, 1.5f);
    }
  }

  // Click: test front-most first (reverse of draw order)
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const ImVec2 mouse = ImGui::GetMousePos();
    for (int i = 5; i >= 0; --i) {
      const float dx = mouse.x - axes[i].screen.x;
      const float dy = mouse.y - axes[i].screen.y;
      if (dx * dx + dy * dy <= ball_r * ball_r) {
        SnapCameraToAxis(camera, axes[i].dir);
        break;
      }
    }
  }
}

}  // namespace Panels

#endif  // AXIS_GIZMO_H
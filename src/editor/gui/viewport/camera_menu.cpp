#include "camera_menu.h"

#include <algorithm>

#include "editor/gui/components/im_components.h"
#include "editor/gui/viewport/action_overlay.h"

namespace Viewport {

void CameraMenu(GodCameraState& state, TransformComponent& transform,
                CameraComponent& camera) {
  IMComponents::PopupSection("Scene Camera");
  if (IMComponents::ButtonBig("Reset Camera")) {
    ResetCamera(state, transform);
    PostAction("Camera reset");
  }
  IMComponents::PopupSlider("Fly Speed", &state.move_speed, 1.0f, 100.0f,
                            "%.1f m/s");

  IMComponents::PopupSection("Projection");
  IMComponents::PopupSlider("Field of View", &camera.fov_, 30.0f, 120.0f,
                            "%.0f°");
  IMComponents::PopupSlider("Near Plane", &camera.near_plane_, 0.01f, 10.0f,
                            "%.2f", true, ImGuiSliderFlags_Logarithmic);
  IMComponents::PopupSlider("Far Plane", &camera.far_plane_, 10.0f, 10000.0f,
                            "%.0f", true, ImGuiSliderFlags_Logarithmic);

  // Keep the depth range valid: near must stay well below far
  camera.near_plane_ = std::min(camera.near_plane_, camera.far_plane_ * 0.5f);

  // Roadmap
  bool orthographic = false;
  IMComponents::PopupCheckbox("Orthographic", &orthographic, false);
}

}  // namespace Viewport
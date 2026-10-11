#ifndef GOD_CAMERA_H
#define GOD_CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct TransformComponent;

// per-session camera state
struct GodCameraState {
  float move_speed = 16.0f;         // 1..100
  float mouse_sensitivity = 0.08f;  // rad/pixel-ish
  float scroll_step = 2.0f;         // speed change per notch
  float axis_smoothing = 5.0f;      // lerp factor
  float keyboard_look_speed = 100.0f;

  glm::vec2 smoothed_axis = glm::vec2(0.0f);

  // Animated view snap
  bool snapping = false;
  float snap_time = 0.0f;
  float snap_duration = 0.25f;  // seconds
  glm::vec3 snap_from_pos{0.0f};
  glm::vec3 snap_to_pos{0.0f};
  glm::quat snap_from_rot{1.0f, 0.0f, 0.0f, 0.0f};
  glm::quat snap_to_rot{1.0f, 0.0f, 0.0f, 0.0f};
};

// Per-frame inputs gathered by EditorUI
struct GodCameraFrameInput {
  bool scene_hovered = false;
  bool right_mouse = false;     // RMB = look/WASD
  bool middle_mouse = false;    // MMB = pan
  glm::vec2 mouse_delta{0.0f};  // pixels this frame
  float mouse_wheel = 0.0f;     // +up, -down
  glm::vec2 move_axis{0.0f};    // x=forward/back (W-S), y=right/left (D-A)
  glm::vec2 look_axis;
  bool hold_shift = false;
};

void CameraUpdateMovement(GodCameraState& state, TransformComponent& transform,
                          const GodCameraFrameInput& input);

void SnapCameraToAxis(GodCameraState& state, TransformComponent& camera,
                      const glm::vec3& axis);

// Animate the camera to look at target from its current direction
void SetCameraTarget(GodCameraState& state, TransformComponent& camera,
                     const glm::vec3& target, float distance);

#endif  // GOD_CAMERA_H
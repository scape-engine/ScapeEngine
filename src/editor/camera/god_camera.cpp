#include "god_camera.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/time/time.h"
#include "engine/transform/transform.h"

namespace {
const glm::vec3 home_position = glm::vec3(0.0f, 2.0f, -10.0f);
}  // namespace

void CameraUpdateMovement(GodCameraState& state, TransformComponent& transform,
                          const GodCameraFrameInput& input) {
  // Timing
  const float dt = Time::UnscaledDeltaf();

  // Animated camera transition (snap, focus)
  if (state.snapping) {
    const bool user_input = input.right_mouse || input.middle_mouse ||
                            input.move_axis != glm::vec2(0.0f) ||
                            input.look_axis != glm::vec2(0.0f);
    if (user_input) {
      state.snapping = false;
    } else {
      state.snap_time += dt;
      const float t =
          glm::clamp(state.snap_time / state.snap_duration, 0.0f, 1.0f);
      const float s = t * t * (3.0f - 2.0f * t);  // smoothstep: ease in/out

      Transform::SetPosition(
          transform, glm::mix(state.snap_from_pos, state.snap_to_pos, s),
          Space::WORLD);
      Transform::SetRotation(
          transform, glm::slerp(state.snap_from_rot, state.snap_to_rot, s),
          Space::WORLD);

      if (t >= 1.0f) {
        state.snapping = false;

        // Store final orientation as (pitch, yaw, 0)
        const glm::vec3 f = state.snap_to_rot * glm::vec3(0.0f, 0.0f, 1.0f);
        Transform::SetEulerAngles(
            transform,
            glm::vec3(glm::degrees(std::asin(-f.y)),
                      glm::degrees(std::atan2(f.x, f.z)), 0.0f),
            Space::WORLD);
      }
      return;
    }
  }

  // Camera basis (WORLD)
  const glm::vec3 F = Transform::Forward(transform, Space::WORLD);
  const glm::vec3 R = Transform::Right(transform, Space::WORLD);
  const glm::vec3 U = Transform::Up(transform, Space::WORLD);

  // Current speed (speedup with SHIFT)
  const float current_speed =
      input.hold_shift ? state.move_speed * 3.0f : state.move_speed;

  // Smooth input axis
  const glm::vec2 desired = input.move_axis;
  const float t = glm::clamp(state.axis_smoothing * dt, 0.0f, 1.0f);
  state.smoothed_axis = glm::mix(state.smoothed_axis, desired, t);

  /* static bool moving = false;

  const bool now = (std::fabs(state.smoothed_axis.x) > 1e-3f) ||
                   (std::fabs(state.smoothed_axis.y) > 1e-3f);
  if (now != moving) {
    Logger::getInstance().Log(
        LogLevel::Debug, now ? "[GodCam] move start" : "[GodCam] move stop");
    moving = now;
  }

  Logger::getInstance().Log(LogLevel::Debug, "Camera Forward: (" +
                                                 std::to_string(F.x) + ", " +
                                                 std::to_string(F.y) + ", " +
                                                 std::to_string(F.z) + ")");
  Logger::getInstance().Log(
      LogLevel::Debug, "Position: (" + std::to_string(transform.position_.x) +
                           ", " + std::to_string(transform.position_.y) + ", " +
                           std::to_string(transform.position_.z) + ")");*/

  // Translate: dir = F * x + R * y
  const glm::vec3 move_dir =
      F * state.smoothed_axis.x + R * state.smoothed_axis.y;

  const glm::vec3 pos_world = Transform::GetPosition(transform, Space::WORLD);
  Transform::SetPosition(transform, pos_world + move_dir * current_speed * dt,
                         Space::WORLD);

  // Keyboard look: Arrow keys to rotate camera
  if (input.look_axis.x != 0.0f || input.look_axis.y != 0.0f) {
    glm::vec3 eulerDeg = Transform::GetEulerAngles(transform, Space::WORLD);

    // ! Check for another way that doesn't invert Y axis
    eulerDeg += glm::vec3(-input.look_axis.y, input.look_axis.x, 0.0f) *
                state.keyboard_look_speed * dt;
    Transform::SetEulerAngles(transform, eulerDeg, Space::WORLD);
  }

  /* if (input.look_axis.x != 0.0f || input.look_axis.y != 0.0f) {
    glm::vec3 eulerDeg = Transform::GetEulerAngles(transform, Space::WORLD);

    Logger::getInstance().Log(
        LogLevel::Debug,
        "LOOK INPUT: axis=(" + std::to_string(input.look_axis.x) + ", " +
            std::to_string(input.look_axis.y) + "), euler_before=(" +
            std::to_string(eulerDeg.x) + ", " + std::to_string(eulerDeg.y) +
            ", " + std::to_string(eulerDeg.z) + ")");

    eulerDeg += glm::vec3(input.look_axis.y, input.look_axis.x, 0.0f) *
                state.keyboard_look_speed * dt;

    Logger::getInstance().Log(LogLevel::Debug,
                              "LOOK OUTPUT: euler_after=(" +
                                  std::to_string(eulerDeg.x) + ", " +
                                  std::to_string(eulerDeg.y) + ", " +
                                  std::to_string(eulerDeg.z) + ")");

    Transform::SetEulerAngles(transform, eulerDeg, Space::WORLD);
  }*/

  // RMB: scroll to change speed, mouse to look
  if (input.right_mouse) {
    if (input.mouse_wheel != 0.0f) {
      state.move_speed =
          glm::clamp(state.move_speed + input.mouse_wheel * state.scroll_step,
                     1.0f, 100.0f);
    }

    // eulerDeg += (-dy, +dx, 0) * sensitivity
    glm::vec3 eulerDeg = Transform::GetEulerAngles(transform, Space::WORLD);
    eulerDeg += glm::vec3(input.mouse_delta.y, input.mouse_delta.x, 0.0f) *
                state.mouse_sensitivity;
    Transform::SetEulerAngles(transform, eulerDeg, Space::WORLD);
  }

  // MMB: pan = (R * -dx + U * -dy) * speed * dt * 0.1
  if (input.middle_mouse) {
    const glm::vec3 pan =
        (R * -input.mouse_delta.x) + (U * -input.mouse_delta.y);
    const glm::vec3 pos2 = Transform::GetPosition(transform, Space::WORLD) +
                           pan * state.move_speed * dt * 0.1f;
    Transform::SetPosition(transform, pos2, Space::WORLD);
  }
}

void StartCameraTransition(GodCameraState& state, TransformComponent& camera,
                           const glm::vec3& to_pos, const glm::quat& to_rot) {
  state.snap_from_pos = Transform::GetPosition(camera, Space::WORLD);
  state.snap_from_rot = Transform::GetRotation(camera, Space::WORLD);
  state.snap_to_pos = to_pos;
  state.snap_to_rot = to_rot;
  state.snap_time = 0.0f;
  state.snapping = true;
  state.smoothed_axis = glm::vec2(0.0f);
}

// Snap the camera to look at the origin from the +axis side, keeping distance
void SnapCameraToAxis(GodCameraState& state, TransformComponent& camera,
                      const glm::vec3& axis) {
  const glm::vec3 pos = Transform::GetPosition(camera, Space::WORLD);
  const float distance = std::max(glm::length(pos), 1.0f);
  const glm::vec3 up = std::fabs(axis.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                 : glm::vec3(0.0f, 1.0f, 0.0f);
  StartCameraTransition(state, camera, axis * distance,
                        glm::quatLookAtLH(-axis, up));
}

void SetCameraTarget(GodCameraState& state, TransformComponent& camera,
                     const glm::vec3& target, float distance) {
  const glm::vec3 pos = Transform::GetPosition(camera, Space::WORLD);
  glm::vec3 dir = target - pos;
  dir = glm::length(dir) > 1e-4f ? glm::normalize(dir)
                                 : Transform::Forward(camera, Space::WORLD);
  const glm::vec3 up = std::fabs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                : glm::vec3(0.0f, 1.0f, 0.0f);
  StartCameraTransition(state, camera, target - dir * distance,
                        glm::quatLookAtLH(dir, up));
}

void ResetCamera(GodCameraState& state, TransformComponent& camera) {
  const glm::vec3 dir = glm::normalize(-home_position);  // toward the origin
  StartCameraTransition(state, camera, home_position,
                        glm::quatLookAtLH(dir, glm::vec3(0.0f, 1.0f, 0.0f)));
}

// (TODO) moving flag
// const float eps = 0.001f;
// const bool moving = (fabs(move_dir.x) > eps) || (fabs(move_dir.y) > eps) ||
// (fabs(move_dir.z) > eps); (void)moving;
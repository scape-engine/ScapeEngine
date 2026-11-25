#include "component_gizmos.h"

#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "editor/registry/component_registry.h"
#include "engine/transform/transform.h"

namespace ComponentGizmos {

// Gizmo colors (Unity-like: lights yellow, cameras light grey)
static const glm::vec3 light_color = glm::vec3(1.0f, 0.93f, 0.45f);
static const glm::vec3 camera_color = glm::vec3(0.85f, 0.85f, 0.85f);

// Directional light: disc facing the light direction, with parallel rays
static constexpr float directional_disc_radius = 0.4f;
static constexpr float directional_ray_length = 1.5f;
static constexpr int directional_ray_count = 8;

// Spot light: inner cone drawn fainter than the outer one
static constexpr float spot_inner_opacity = 0.4f;

void DrawSceneViewIcons(IMGizmo& gizmos, TransformComponent& camera_transform) {
  // Try draw scene icon of every component instance
  for (const auto& [name, component] : ComponentRegistry::Get()) {
    component.try_draw_scene_icons(gizmos, camera_transform);
  }
}

void DrawEntityGizmos(IMGizmo& gizmos, Entity entity, bool draw_cameras,
                      bool draw_lights, float camera_aspect) {
  ECS& world = ECS::Main();
  if (!world.Has<TransformComponent>(entity))
    return;

  // Always visible, even inside or behind geometry
  gizmos.foreground = true;
  gizmos.opacity = 1.0f;

  TransformComponent& transform = world.Get<TransformComponent>(entity);

  if (draw_cameras && world.Has<CameraComponent>(entity))
    DrawCamera(gizmos, transform, world.Get<CameraComponent>(entity),
               camera_aspect);

  if (draw_lights) {
    if (world.Has<DirectionalLightComponent>(entity))
      DrawDirectionalLight(gizmos, transform,
                           world.Get<DirectionalLightComponent>(entity));
    if (world.Has<PointLightComponent>(entity))
      DrawPointLight(gizmos, transform,
                     world.Get<PointLightComponent>(entity));
    if (world.Has<SpotlightComponent>(entity))
      DrawSpotlight(gizmos, transform, world.Get<SpotlightComponent>(entity));
  }
}

void DrawCamera(IMGizmo& gizmos, TransformComponent& transform,
                CameraComponent& camera, float aspect) {
  gizmos.color = camera_color;
  gizmos.opacity = camera.enabled_ ? 1.0f : 0.4f;
  gizmos.Frustum(Transform::GetPosition(transform, Space::WORLD),
                 Transform::GetRotation(transform, Space::WORLD), camera.fov_,
                 aspect, camera.near_plane_, camera.far_plane_);
}

void DrawDirectionalLight(IMGizmo& gizmos, TransformComponent& transform,
                          DirectionalLightComponent& light) {
  const glm::vec3 position = Transform::GetPosition(transform, Space::WORLD);
  const glm::quat rotation = Transform::GetRotation(transform, Space::WORLD);
  const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
  const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
  const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);

  gizmos.color = light_color;
  gizmos.opacity = light.enabled_ ? 1.0f : 0.4f;
  gizmos.Circle(position, forward, directional_disc_radius);

  // Rays leave the disc's rim along the light direction, plus one from the
  // center
  gizmos.Line(position, position + forward * directional_ray_length);
  for (int i = 0; i < directional_ray_count; ++i) {
    const float angle = glm::two_pi<float>() * static_cast<float>(i) /
                        static_cast<float>(directional_ray_count);
    const glm::vec3 start =
        position + (right * std::cos(angle) + up * std::sin(angle)) *
                       directional_disc_radius;
    gizmos.Line(start, start + forward * directional_ray_length);
  }
}

void DrawPointLight(IMGizmo& gizmos, TransformComponent& transform,
                    PointLightComponent& light) {
  gizmos.color = light_color;
  gizmos.opacity = light.enabled_ ? 1.0f : 0.4f;
  gizmos.WireSphere(Transform::GetPosition(transform, Space::WORLD),
                    light.range_,
                    Transform::GetRotation(transform, Space::WORLD));
}

void DrawSpotlight(IMGizmo& gizmos, TransformComponent& transform,
                   SpotlightComponent& light) {
  const glm::vec3 position = Transform::GetPosition(transform, Space::WORLD);
  const glm::quat rotation = Transform::GetRotation(transform, Space::WORLD);
  const float opacity = light.enabled_ ? 1.0f : 0.4f;

  // Angles are measured from the light's axis, as in the lit shader
  gizmos.color = light_color;
  gizmos.opacity = opacity;
  gizmos.WireCone(position, rotation, light.range_, light.outer_angle_);

  gizmos.opacity = opacity * spot_inner_opacity;
  gizmos.WireCone(position, rotation, light.range_, light.inner_angle_);
}

}  // namespace ComponentGizmos

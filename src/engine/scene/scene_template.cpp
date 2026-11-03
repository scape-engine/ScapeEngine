// ---------------------------------------------------------------------------
// scene_template.cpp
// Implements the default template scene logic used when a new scene is created
// in the editor.
// Purpose: Provides placeholder setup, update, and render behavior
// ---------------------------------------------------------------------------

#include "scene_template.h"

#include "engine/core/logger.h"
#include "engine/ecs/ecs_collection.h"
#include "engine/transform/transform.h"

void SceneTemplate::Create() {
  Logger::getInstance().Log(LogLevel::Info, "[SceneTemplate] Create()");
  // Load test mesh, set up test light, etc.

  auto& world = ECS::Main();

  // Sky light
  auto [sky_entity, sky_transform] = world.CreateEntity("SkyLight");
  auto& sky_light = world.Add<SkyLightComponent>(sky_entity);
  sky_light.enabled_ = true;
  sky_light.real_time_capture_ = true;
  sky_light.intensity_ = 1.0f;

  // Directional light (sun)
  auto [sun_entity, sun_transform] = world.CreateEntity("Sun");
  glm::vec3 light_dir = glm::normalize(glm::vec3(0.5f, -0.7f, 0.3f));
  Transform::SetRotation(sun_transform,
                         Transform::LookAt(glm::vec3(0.0f), light_dir),
                         Space::LOCAL);

  auto& dir_light = world.Add<DirectionalLightComponent>(sun_entity);
  dir_light.enabled_ = true;
  dir_light.color_ = glm::vec3(1.0f, 0.95f, 0.9f);
  dir_light.intensity_ = 1.0f;

  Logger::getInstance().Log(LogLevel::Info,
                            "[SceneTemplate] Default lighting created");

  /* Logger::getInstance().Log(
      LogLevel::Info,
      "SkyLight enabled: " + std::to_string(sky_light.enabled_) + ", color: (" +
          std::to_string(sky_light.color_.x) + ", " +
          std::to_string(sky_light.color_.y) + ", " +
          std::to_string(sky_light.color_.z) + ")");

  Logger::getInstance().Log(
      LogLevel::Info,
      "DirectionalLight enabled: " + std::to_string(dir_light.enabled_) +
          ", color: (" + std::to_string(dir_light.color_.x) + ", " +
          std::to_string(dir_light.color_.y) + ", " +
          std::to_string(dir_light.color_.z) + ")");*/
}

void SceneTemplate::Update(float dt) {
  // Animate object? Rotate light? etc
}

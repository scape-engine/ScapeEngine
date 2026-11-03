#ifndef LIGHT_COMPONENT_H
#define LIGHT_COMPONENT_H

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

enum class ShadowResolution : uint16_t {
  NONE = 0,
  LOW = 1024,
  MEDIUM = 2048,
  HIGH = 4096
};

// Directional light: Direction comes from Transform's forward vector
struct DirectionalLightComponent {
  bool enabled_ = true;
  glm::vec3 color_ = glm::vec3(1.0f);
  float intensity_ = 5.0f;  // radiance scale

  // Shadows
  // light-specific parameters
  bool cast_shadows_ = true;
  float shadow_bias_ = 0.0005f;
  float normal_bias_ = 0.03f;
  ShadowResolution shadow_resolution_ = ShadowResolution::MEDIUM;
  float shadow_softness_ = 1.0f;  // PCF radius in texels
};

// Point light: position comes from Transform.position
struct PointLightComponent {
  bool enabled_ = true;
  glm::vec3 color_ = glm::vec3(1.0f);
  float intensity_ = 1.0f;
  float range_ = 10.0f;  // meters
  float falloff_ = 5.0f;

  bool cast_shadows_ = false;
  float shadow_bias_ = 0.0005f;
  ShadowResolution shadow_resolution_ = ShadowResolution::LOW;
  float shadow_softness_ = 1.0f;
};

// Spot light: position+direction come from Transform
struct SpotlightComponent {
  bool enabled_ = true;
  glm::vec3 color_ = glm::vec3(1.0f);
  float intensity_ = 1.0f;
  float range_ = 10.0f;
  float falloff_ = 5.0f;
  float inner_angle_ = 45.0f;  // degrees
  float outer_angle_ = 60.0f;  // degrees

  bool cast_shadows_ = false;
  float shadow_bias_ = 0.0005f;
  ShadowResolution shadow_resolution_ = ShadowResolution::LOW;
  float shadow_softness_ = 1.0f;
};

// Where the sky light gets its lighting from (UE: Source Type)
enum class SkyLightSource {
  CaptureScene,  // the sky rendered by the skybox
  SourceCubemap  // an equirect (latitude/longitude) image, e.g. an HDRI
};

// Captures the sky and lights the scene with it (diffuse + reflections), like
// UE's Sky Light. Only one sky light is used per scene
struct SkyLightComponent {
  bool enabled_ = true;
  glm::vec3 color_ = glm::vec3(1.0f);  // tint of the captured sky
  float intensity_ = 1.0f;             // ambient radiance scale

  bool cast_shadows_ = true;

  // Source of the lighting
  SkyLightSource source_type_ = SkyLightSource::CaptureScene;
  std::string cubemap_path_;           // absolute path, SourceCubemap only
  float source_cubemap_angle_ = 0.0f;  // degrees around the up axis

  // Recapture every frame (CaptureScene only), otherwise only when recapture_
  // is set
  bool real_time_capture_ = false;
  bool recapture_ = false;  // consumed by the next capture

  // Replace light from below the horizon (applied when captured)
  bool lower_hemisphere_is_solid_color_ = true;
  glm::vec3 lower_hemisphere_color_ = glm::vec3(0.0f);

  // TODO: cubemap res, affects world, volumetric scattering intensity
};

#endif  // LIGHT_COMPONENT_H

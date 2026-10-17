#include "light.h"

#include <algorithm>

#include "engine/core/logger.h"

/**************************************************************************
 *  Multi-Light System
 * ----------
 * ! Experimental implementation supporting multiple lighting types
 * reference:
 * https://www.rick.me.uk/posts/2021/07/3d-game-engine-devlog-part-4-multiple-lights/
 **************************************************************************/

namespace Light {

namespace {

// ? Should match shaders
// Maximum number of lights supported in one frame
constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 4;
constexpr uint32_t MAX_POINT_LIGHTS = 32;
constexpr uint32_t MAX_SPOT_LIGHTS = 16;

// Uniform handles
bgfx::UniformHandle u_ambient = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lightCounts = BGFX_INVALID_HANDLE;

bgfx::UniformHandle u_dirLight_direction = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_dirLight_ambient = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_dirLight_diffuse = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_dirLight_specular = BGFX_INVALID_HANDLE;

bgfx::UniformHandle u_pointLight_position = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_pointLight_ambient = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_pointLight_diffuse = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_pointLight_specular = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_pointLight_attenuation = BGFX_INVALID_HANDLE;

bgfx::UniformHandle u_spotLight_position = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_spotLight_direction = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_spotLight_ambient = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_spotLight_diffuse = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_spotLight_specular = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_spotLight_attenuation = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_spotLight_cutoff = BGFX_INVALID_HANDLE;

// Data arrays
float ambient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
float lightCounts[4] = {0.0f, 0.0f, 0.0f, 0.0f};

float dirLightDirection[MAX_DIRECTIONAL_LIGHTS * 4] = {0};
float dirLightAmbient[MAX_DIRECTIONAL_LIGHTS * 4] = {0};
float dirLightDiffuse[MAX_DIRECTIONAL_LIGHTS * 4] = {0};
float dirLightSpecular[MAX_DIRECTIONAL_LIGHTS * 4] = {0};

float pointLightPosition[MAX_POINT_LIGHTS * 4] = {0};
float pointLightAmbient[MAX_POINT_LIGHTS * 4] = {0};
float pointLightDiffuse[MAX_POINT_LIGHTS * 4] = {0};
float pointLightSpecular[MAX_POINT_LIGHTS * 4] = {0};
float pointLightAttenuation[MAX_POINT_LIGHTS * 4] = {0};

float spotLightPosition[MAX_SPOT_LIGHTS * 4] = {0};
float spotLightDirection[MAX_SPOT_LIGHTS * 4] = {0};
float spotLightAmbient[MAX_SPOT_LIGHTS * 4] = {0};
float spotLightDiffuse[MAX_SPOT_LIGHTS * 4] = {0};
float spotLightSpecular[MAX_SPOT_LIGHTS * 4] = {0};
float spotLightAttenuation[MAX_SPOT_LIGHTS * 4] = {0};
float spotLightCutoff[MAX_SPOT_LIGHTS * 4] = {0};
}  // namespace

// Smooth interpolation for transitions
static float SmoothStepRange(float edge0, float edge1, float x) {
  float t = (x - edge0) / (edge1 - edge0);
  t = bx::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

void Create() {
  // TODO: Move this! Uniforms should be created in GraphicsRenderer
  u_ambient = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
  u_lightCounts = bgfx::createUniform("u_lightCounts", bgfx::UniformType::Vec4);

  u_dirLight_direction = bgfx::createUniform(
      "u_dirLight_direction", bgfx::UniformType::Vec4, MAX_DIRECTIONAL_LIGHTS);
  u_dirLight_ambient = bgfx::createUniform(
      "u_dirLight_ambient", bgfx::UniformType::Vec4, MAX_DIRECTIONAL_LIGHTS);
  u_dirLight_diffuse = bgfx::createUniform(
      "u_dirLight_diffuse", bgfx::UniformType::Vec4, MAX_DIRECTIONAL_LIGHTS);
  u_dirLight_specular = bgfx::createUniform(
      "u_dirLight_specular", bgfx::UniformType::Vec4, MAX_DIRECTIONAL_LIGHTS);

  u_pointLight_position = bgfx::createUniform(
      "u_pointLight_position", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);
  u_pointLight_ambient = bgfx::createUniform(
      "u_pointLight_ambient", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);
  u_pointLight_diffuse = bgfx::createUniform(
      "u_pointLight_diffuse", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);
  u_pointLight_specular = bgfx::createUniform(
      "u_pointLight_specular", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);
  u_pointLight_attenuation = bgfx::createUniform(
      "u_pointLight_attenuation", bgfx::UniformType::Vec4, MAX_POINT_LIGHTS);

  u_spotLight_position = bgfx::createUniform(
      "u_spotLight_position", bgfx::UniformType::Vec4, MAX_SPOT_LIGHTS);
  u_spotLight_direction = bgfx::createUniform(
      "u_spotLight_direction", bgfx::UniformType::Vec4, MAX_SPOT_LIGHTS);
  u_spotLight_ambient = bgfx::createUniform(
      "u_spotLight_ambient", bgfx::UniformType::Vec4, MAX_SPOT_LIGHTS);
  u_spotLight_diffuse = bgfx::createUniform(
      "u_spotLight_diffuse", bgfx::UniformType::Vec4, MAX_SPOT_LIGHTS);
  u_spotLight_specular = bgfx::createUniform(
      "u_spotLight_specular", bgfx::UniformType::Vec4, MAX_SPOT_LIGHTS);
  u_spotLight_attenuation = bgfx::createUniform(
      "u_spotLight_attenuation", bgfx::UniformType::Vec4, MAX_SPOT_LIGHTS);
  u_spotLight_cutoff = bgfx::createUniform(
      "u_spotLight_cutoff", bgfx::UniformType::Vec4, MAX_SPOT_LIGHTS);

  Logger::getInstance().Log(LogLevel::Info, "Light: Uniforms created");
}

void Destroy() {
  auto destroy = [](auto& h) {
    if (bgfx::isValid(h)) {
      bgfx::destroy(h);
      h = BGFX_INVALID_HANDLE;
    }
  };

  destroy(u_ambient);
  destroy(u_lightCounts);
  destroy(u_dirLight_direction);
  destroy(u_dirLight_ambient);
  destroy(u_dirLight_diffuse);
  destroy(u_dirLight_specular);
  destroy(u_pointLight_position);
  destroy(u_pointLight_ambient);
  destroy(u_pointLight_diffuse);
  destroy(u_pointLight_specular);
  destroy(u_pointLight_attenuation);
  destroy(u_spotLight_position);
  destroy(u_spotLight_direction);
  destroy(u_spotLight_ambient);
  destroy(u_spotLight_diffuse);
  destroy(u_spotLight_specular);
  destroy(u_spotLight_attenuation);
  destroy(u_spotLight_cutoff);
}

void SetDirectionalLight(int index, const glm::vec3& direction,
                         const glm::vec3& amb, const glm::vec3& diff,
                         const glm::vec3& spec) {
  if (index < 0 || index >= (int)MAX_DIRECTIONAL_LIGHTS)
    return;

  int i = index * 4;
  dirLightDirection[i] = direction.x;
  dirLightDirection[i + 1] = direction.y;
  dirLightDirection[i + 2] = direction.z;

  dirLightAmbient[i] = amb.r;
  dirLightAmbient[i + 1] = amb.g;
  dirLightAmbient[i + 2] = amb.b;

  dirLightDiffuse[i] = diff.r;
  dirLightDiffuse[i + 1] = diff.g;
  dirLightDiffuse[i + 2] = diff.b;

  dirLightSpecular[i] = spec.r;
  dirLightSpecular[i + 1] = spec.g;
  dirLightSpecular[i + 2] = spec.b;
}

void SetPointLight(int index, const glm::vec3& position, const glm::vec3& amb,
                   const glm::vec3& diff, const glm::vec3& spec, float constant,
                   float linear, float quadratic) {
  if (index < 0 || index >= (int)MAX_POINT_LIGHTS)
    return;

  int i = index * 4;
  pointLightPosition[i] = position.x;
  pointLightPosition[i + 1] = position.y;
  pointLightPosition[i + 2] = position.z;

  pointLightAmbient[i] = amb.r;
  pointLightAmbient[i + 1] = amb.g;
  pointLightAmbient[i + 2] = amb.b;

  pointLightDiffuse[i] = diff.r;
  pointLightDiffuse[i + 1] = diff.g;
  pointLightDiffuse[i + 2] = diff.b;

  pointLightSpecular[i] = spec.r;
  pointLightSpecular[i + 1] = spec.g;
  pointLightSpecular[i + 2] = spec.b;

  pointLightAttenuation[i] = constant;
  pointLightAttenuation[i + 1] = linear;
  pointLightAttenuation[i + 2] = quadratic;
}

void SetSpotLight(int index, const glm::vec3& position,
                  const glm::vec3& direction, const glm::vec3& amb,
                  const glm::vec3& diff, const glm::vec3& spec,
                  float innerCutoff, float outerCutoff, float constant,
                  float linear, float quadratic) {
  if (index < 0 || index >= (int)MAX_SPOT_LIGHTS)
    return;

  int i = index * 4;
  spotLightPosition[i] = position.x;
  spotLightPosition[i + 1] = position.y;
  spotLightPosition[i + 2] = position.z;

  spotLightDirection[i] = direction.x;
  spotLightDirection[i + 1] = direction.y;
  spotLightDirection[i + 2] = direction.z;

  spotLightAmbient[i] = amb.r;
  spotLightAmbient[i + 1] = amb.g;
  spotLightAmbient[i + 2] = amb.b;

  spotLightDiffuse[i] = diff.r;
  spotLightDiffuse[i + 1] = diff.g;
  spotLightDiffuse[i + 2] = diff.b;

  spotLightSpecular[i] = spec.r;
  spotLightSpecular[i + 1] = spec.g;
  spotLightSpecular[i + 2] = spec.b;

  spotLightAttenuation[i] = constant;
  spotLightAttenuation[i + 1] = linear;
  spotLightAttenuation[i + 2] = quadratic;

  spotLightCutoff[i] = innerCutoff;
  spotLightCutoff[i + 1] = outerCutoff;
}

void SetLightCounts(int dirCount, int pointCount, int spotCount) {
  // Clamp so shaders never loop past the uniform arrays
  lightCounts[0] = static_cast<float>(std::min(dirCount, (int)MAX_DIRECTIONAL_LIGHTS));
  lightCounts[1] = static_cast<float>(std::min(pointCount, (int)MAX_POINT_LIGHTS));
  lightCounts[2] = static_cast<float>(std::min(spotCount, (int)MAX_SPOT_LIGHTS));
}

void SetAmbient(const glm::vec3& color) {
  ambient[0] = color.r;
  ambient[1] = color.g;
  ambient[2] = color.b;
}

void ApplyUniforms() {
  bgfx::setUniform(u_ambient, ambient);
  bgfx::setUniform(u_lightCounts, lightCounts);

  if (lightCounts[0] > 0) {
    bgfx::setUniform(u_dirLight_direction, dirLightDirection,
                     MAX_DIRECTIONAL_LIGHTS);
    bgfx::setUniform(u_dirLight_ambient, dirLightAmbient,
                     MAX_DIRECTIONAL_LIGHTS);
    bgfx::setUniform(u_dirLight_diffuse, dirLightDiffuse,
                     MAX_DIRECTIONAL_LIGHTS);
    bgfx::setUniform(u_dirLight_specular, dirLightSpecular,
                     MAX_DIRECTIONAL_LIGHTS);
  }

  if (lightCounts[1] > 0) {
    bgfx::setUniform(u_pointLight_position, pointLightPosition,
                     MAX_POINT_LIGHTS);
    bgfx::setUniform(u_pointLight_ambient, pointLightAmbient, MAX_POINT_LIGHTS);
    bgfx::setUniform(u_pointLight_diffuse, pointLightDiffuse, MAX_POINT_LIGHTS);
    bgfx::setUniform(u_pointLight_specular, pointLightSpecular,
                     MAX_POINT_LIGHTS);
    bgfx::setUniform(u_pointLight_attenuation, pointLightAttenuation,
                     MAX_POINT_LIGHTS);
  }

  if (lightCounts[2] > 0) {
    bgfx::setUniform(u_spotLight_position, spotLightPosition, MAX_SPOT_LIGHTS);
    bgfx::setUniform(u_spotLight_direction, spotLightDirection,
                     MAX_SPOT_LIGHTS);
    bgfx::setUniform(u_spotLight_ambient, spotLightAmbient, MAX_SPOT_LIGHTS);
    bgfx::setUniform(u_spotLight_diffuse, spotLightDiffuse, MAX_SPOT_LIGHTS);
    bgfx::setUniform(u_spotLight_specular, spotLightSpecular, MAX_SPOT_LIGHTS);
    bgfx::setUniform(u_spotLight_attenuation, spotLightAttenuation,
                     MAX_SPOT_LIGHTS);
    bgfx::setUniform(u_spotLight_cutoff, spotLightCutoff, MAX_SPOT_LIGHTS);
  }
}

}  // namespace Light

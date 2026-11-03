#ifndef LIGHT_H
#define LIGHT_H

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <glm/glm.hpp>

namespace Light {

void Create();
void Destroy();

// Set light data at specific array index
void SetDirectionalLight(int index, const glm::vec3& direction,
                         const glm::vec3& ambient, const glm::vec3& diffuse,
                         const glm::vec3& specular);

void SetPointLight(int index, const glm::vec3& position,
                   const glm::vec3& ambient, const glm::vec3& diffuse,
                   const glm::vec3& specular, float constant, float linear,
                   float quadratic);

void SetSpotLight(int index, const glm::vec3& position,
                  const glm::vec3& direction, const glm::vec3& ambient,
                  const glm::vec3& diffuse, const glm::vec3& specular,
                  float innerCutoff, float outerCutoff, float constant,
                  float linear, float quadratic);

// Set counts (ambient comes from the sky light, see sky_light.h)
void SetLightCounts(int dirCount, int pointCount, int spotCount);

void ApplyUniforms();

// Getters
bgfx::UniformHandle GetSunDirectionUniform();
bgfx::UniformHandle GetSunLuminanceUniform();
bgfx::UniformHandle GetColorsUniform();
bgfx::UniformHandle GetCountsUniform();
}  // namespace Light

#endif  // LIGHT_H

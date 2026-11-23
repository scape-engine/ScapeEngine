#ifndef WATER_MATERIAL_H
#define WATER_MATERIAL_H

#include <glm/glm.hpp>

#include "engine/renderer/material/material.h"

// What a water surface looks like
struct WaterMaterialData {
  glm::vec3 colour{0.02f, 0.09f, 0.12f};  // linear
  float roughness = 0.05f;

  bool operator==(const WaterMaterialData&) const = default;
};

// Water surface material, shaded by vs_water_surface / fs_water_surface and
// lit as the standard material is. The mesh is a grid on the model's xz
// plane, displaced by the entity's wave cascades (assets/shaders/includes/
// water_waves.sh: its ShaderParamsComponent and RenderTextureComponent)
class WaterMaterial {
public:
  static Material Build(const WaterMaterialData& data);
};

#endif  // WATER_MATERIAL_H

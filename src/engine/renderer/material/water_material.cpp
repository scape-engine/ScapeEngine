#include "water_material.h"

#include "engine/context/engine_context.h"
#include "engine/renderer/graphics_renderer.h"

Material WaterMaterial::Build(const WaterMaterialData& data) {
  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());

  Material material;
  material.name = "Water";
  material.program = EngineContext::Shader().LoadProgram(
      "water_surface", "vs_water_surface.bin", "fs_water_surface.bin");
  // Must match fs_water_surface.sc
  material.params = {
      MaterialParam{
          graphics.CreateUniform("u_meshColor", bgfx::UniformType::Vec4),
          glm::vec4(data.colour, 1.0f)},
      MaterialParam{
          graphics.CreateUniform("u_materialParams", bgfx::UniformType::Vec4),
          glm::vec4(0.0f, data.roughness, 0.0f, 0.0f)},
  };
  // Seen from below too
  material.double_sided = true;
  return material;
}

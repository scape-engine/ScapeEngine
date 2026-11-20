#include "terrain_material.h"

#include <cstdint>

#include "engine/context/engine_context.h"
#include "engine/renderer/graphics_renderer.h"

bgfx::TextureHandle TerrainMaterial::CreateTileColours(
    const TerrainMaterialData& data) {
  uint8_t texels[TerrainMaterialData::tile_count][4];
  for (int i = 0; i < TerrainMaterialData::tile_count; ++i) {
    const glm::vec3 c = glm::clamp(data.tile_colours[i], 0.0f, 1.0f) * 255.0f;
    texels[i][0] = static_cast<uint8_t>(c.r + 0.5f);
    texels[i][1] = static_cast<uint8_t>(c.g + 0.5f);
    texels[i][2] = static_cast<uint8_t>(c.b + 0.5f);
    texels[i][3] = 255;
  }
  // Point sampled: one texel per tile
  return bgfx::createTexture2D(
      TerrainMaterialData::tile_count, 1, false, 1, bgfx::TextureFormat::RGBA8,
      BGFX_TEXTURE_SRGB | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
      bgfx::copy(texels, sizeof(texels)));
}

Material TerrainMaterial::Build(const TerrainMaterialData& data,
                                bgfx::TextureHandle tile_colours) {
  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());

  Material material;
  material.name = data.name;
  material.program = EngineContext::Shader().LoadProgram(
      "terrain_tiles", "vs_terrain_tiles.bin", "fs_terrain_tiles.bin");

  // Must match fs_terrain_tiles.sc
  material.params = {
      MaterialParam{
          graphics.CreateUniform("u_terrainParams", bgfx::UniformType::Vec4),
          glm::vec4(data.roughness, 0.0f, 0.0f, 0.0f)},
      MaterialParam{
          graphics.CreateUniform("u_tileNoise", bgfx::UniformType::Vec4),
          data.tile_noise},
  };
  // Not a registry texture: the fallback is the one always bound
  material.textures = {MaterialTexture{
      graphics.CreateUniform("s_tileColours", bgfx::UniformType::Sampler), 0,
      INVALID_TEXTURE, tile_colours}};
  return material;
}

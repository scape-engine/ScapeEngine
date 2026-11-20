#ifndef TERRAIN_MATERIAL_H
#define TERRAIN_MATERIAL_H

#include <array>
#include <glm/glm.hpp>
#include <string>

#include "engine/renderer/material/material.h"

// What a tiled terrain looks like: the colour of each of its tiles, and how
// they mix (see assets/shaders/includes/terrain_tiles.sh)
struct TerrainMaterialData {
  static constexpr int tile_count = 23;

  std::string name;
  std::array<glm::vec3, tile_count> tile_colours{};  // sRGB
  float roughness = 0.9f;
  // Which look of a tile shows, from two noise samples: x, y the samples'
  // frequencies (per metre), z the second sample's weight, w an offset
  glm::vec4 tile_noise{0.05f, 0.013f, 0.5f, 0.0f};

  bool operator==(const TerrainMaterialData&) const = default;
};

// Tiled terrain material, shaded by vs_terrain_tiles / fs_terrain_tiles and
// lit as the standard material is. The vertices carry two tile groups and a
// blend (TexCoord0) and the position relative to the terrain's centre
// (TexCoord2), which sets the up direction and the noise
class TerrainMaterial {
public:
  // The tile colours as a texture the material reads (tile_count x 1, sRGB).
  // The caller owns it and destroys it after the material
  static bgfx::TextureHandle CreateTileColours(const TerrainMaterialData& data);

  static Material Build(const TerrainMaterialData& data,
                        bgfx::TextureHandle tile_colours);
};

#endif  // TERRAIN_MATERIAL_H

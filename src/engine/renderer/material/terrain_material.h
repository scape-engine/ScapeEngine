#ifndef TERRAIN_MATERIAL_H
#define TERRAIN_MATERIAL_H

#include <array>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>

#include "engine/renderer/material/material.h"

// What a tiled terrain looks like: each tile's image and colour, and how
// they mix (see assets/shaders/includes/terrain_tiles.sh)
struct TerrainMaterialData {
  static constexpr int tile_count = 23;

  std::string name;
  std::array<glm::vec3, tile_count> tile_colours{};  // sRGB
  // Each tile's image, project-relative: colour in RGB, height in alpha. The
  // tile colour recolours it (hue and value over the image's mean, saturation
  // as a ceiling). Empty: the tile is its flat colour. Images share one size
  std::array<std::string, tile_count> tile_images{};
  // Each tile's heights, as the blends read them: shifted by the offset, then
  // scaled about 0.5 by 1 + scale
  std::array<float, tile_count> height_offset{};
  std::array<float, tile_count> height_scale{};
  float roughness = 0.9f;
  // Which look of a tile shows, from two noise samples: x, y the samples'
  // frequencies (per metre), z the second sample's weight, w an offset
  glm::vec4 tile_noise{0.05f, 0.013f, 0.5f, 0.0f};

  bool operator==(const TerrainMaterialData&) const = default;
};

// The tile images on the GPU: one array layer per tile, with mips
struct TerrainTileImages {
  bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
  std::array<glm::vec3, TerrainMaterialData::tile_count> average{};  // HSV
  std::array<bool, TerrainMaterialData::tile_count> loaded{};
};

// Tiled terrain material, shaded by vs_terrain_tiles / fs_terrain_tiles and
// lit as the standard material is. The vertices carry two tile groups and a
// blend (TexCoord0) and the position relative to the terrain's centre
// (TexCoord2), which sets the up direction, the image projection and the
// noise. The caller owns the textures and destroys them after the material
class TerrainMaterial {
public:
  // Loads data.tile_images from `root`; a tile whose image is missing or
  // another size than the first shows its flat colour (`error` says which)
  static TerrainTileImages LoadTileImages(const TerrainMaterialData& data,
                                          const std::filesystem::path& root,
                                          std::string& error);

  // The per-tile values the shader reads (tile_count x 3, floats)
  static bgfx::TextureHandle CreateTileConstants(
      const TerrainMaterialData& data, const TerrainTileImages& images);

  static Material Build(const TerrainMaterialData& data,
                        bgfx::TextureHandle tile_constants,
                        bgfx::TextureHandle tile_images);
};

#endif  // TERRAIN_MATERIAL_H

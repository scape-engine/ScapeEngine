#include "terrain_material.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "engine/content/loaders/image_loader.h"
#include "engine/context/engine_context.h"
#include "engine/renderer/graphics_renderer.h"

// As terrain_tiles.sh's TerrainRgbToHsv
static glm::vec3 _RgbToHsv(const glm::vec3& rgb) {
  const float hi = std::max(rgb.r, std::max(rgb.g, rgb.b));
  const float lo = std::min(rgb.r, std::min(rgb.g, rgb.b));
  const float delta = hi - lo;
  if (hi == 0.0f || delta == 0.0f)
    return {0.0f, hi == 0.0f ? 0.0f : delta / hi, hi};
  float h = rgb.r == hi   ? (rgb.g - rgb.b) / delta
            : rgb.g == hi ? (rgb.b - rgb.r) / delta + 2.0f
                          : (rgb.r - rgb.g) / delta + 4.0f;
  if (h < 0.0f)
    h += 6.0f;
  return {h / 6.0f, delta / hi, hi};
}

// Half the size (at least 1), each texel the mean of up to 2 x 2
static std::vector<uint8_t> _HalfSize(const std::vector<uint8_t>& texels,
                                      int width, int height) {
  const int w = std::max(width / 2, 1), h = std::max(height / 2, 1);
  std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
      const int y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
      for (int c = 0; c < 4; ++c) {
        auto at = [&](int xx, int yy) {
          return texels[(static_cast<size_t>(yy) * width + xx) * 4 + c];
        };
        out[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8_t>(
            (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1) + 2) / 4);
      }
    }
  }
  return out;
}

TerrainTileImages TerrainMaterial::LoadTileImages(
    const TerrainMaterialData& data, const std::filesystem::path& root,
    std::string& error) {
  constexpr int count = TerrainMaterialData::tile_count;
  TerrainTileImages images;
  std::array<Content::ImageData, count> layers;
  int width = 0, height = 0;
  for (int i = 0; i < count; ++i) {
    if (data.tile_images[i].empty())
      continue;
    const std::filesystem::path path = root / data.tile_images[i];
    Content::ImageData image = Content::ImageLoader::LoadFile(path);
    if (!image.Valid() || image.format != Content::ImageFormat::RGBA8) {
      error += "Can't load " + path.string() + "\n";
      continue;
    }
    if (width == 0) {
      width = image.width;
      height = image.height;
    } else if (image.width != width || image.height != height) {
      error += path.string() + " is not " + std::to_string(width) + " x " +
               std::to_string(height) + "\n";
      continue;
    }
    // The mean colour, as HSV
    glm::dvec3 sum(0.0);
    for (size_t t = 0; t < image.pixels.size(); t += 4)
      sum += glm::dvec3(_RgbToHsv(glm::vec3(image.pixels[t], image.pixels[t + 1],
                                            image.pixels[t + 2]) /
                                  255.0f));
    images.average[i] = glm::vec3(sum / double(image.pixels.size() / 4));
    images.loaded[i] = true;
    layers[i] = std::move(image);
  }
  if (width == 0)
    width = height = 4;

  // Layer by layer, each with its mips; tiles without an image are white at
  // mid height (the shader shows their flat colour)
  int mips = 1;
  while ((std::max(width, height) >> mips) > 0)
    ++mips;
  std::vector<uint8_t> texels;
  for (int i = 0; i < count; ++i) {
    std::vector<uint8_t> level =
        images.loaded[i] ? std::move(layers[i].pixels)
                         : std::vector<uint8_t>(size_t(width) * height * 4, 255);
    if (!images.loaded[i])
      for (size_t t = 3; t < level.size(); t += 4)
        level[t] = 128;
    int w = width, h = height;
    for (int m = 0; m < mips; ++m) {
      texels.insert(texels.end(), level.begin(), level.end());
      if (m + 1 < mips) {
        level = _HalfSize(level, w, h);
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
      }
    }
  }
  // Colours are read as stored: the shader recolours them in sRGB values
  images.texture = bgfx::createTexture2D(
      static_cast<uint16_t>(width), static_cast<uint16_t>(height), true, count,
      bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
      bgfx::copy(texels.data(), static_cast<uint32_t>(texels.size())));
  return images;
}

bgfx::TextureHandle TerrainMaterial::CreateTileConstants(
    const TerrainMaterialData& data, const TerrainTileImages& images) {
  constexpr int count = TerrainMaterialData::tile_count;
  glm::vec4 texels[3][count];
  for (int i = 0; i < count; ++i) {
    texels[0][i] = glm::vec4(
        _RgbToHsv(glm::clamp(data.tile_colours[i], 0.0f, 1.0f)),
        data.height_offset[i]);
    texels[1][i] = glm::vec4(images.average[i], data.height_scale[i]);
    texels[2][i] = glm::vec4(images.loaded[i] ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
  }
  return bgfx::createTexture2D(
      count, 3, false, 1, bgfx::TextureFormat::RGBA32F,
      BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
      bgfx::copy(texels, sizeof(texels)));
}

Material TerrainMaterial::Build(const TerrainMaterialData& data,
                                bgfx::TextureHandle tile_constants,
                                bgfx::TextureHandle tile_images) {
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
  // Not registry textures: the fallbacks are the ones always bound
  material.textures = {
      MaterialTexture{
          graphics.CreateUniform("s_tileConstants", bgfx::UniformType::Sampler),
          0, INVALID_TEXTURE, tile_constants},
      MaterialTexture{
          graphics.CreateUniform("s_tileImages", bgfx::UniformType::Sampler),
          1, INVALID_TEXTURE, tile_images},
  };
  return material;
}

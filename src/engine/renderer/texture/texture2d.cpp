#include "texture2d.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "engine/core/logger.h"

#include "engine/content/loaders/image_loader.h"

// TODO: texture2d should be using EngineContext

// Color textures are authored in sRGB and must be linearized when sampled
// (glTF 2.0 spec, Materials: base color and emissive textures are sRGB)
static bool IsSRGB(TextureType type) {
  return type == TextureType::ALBEDO || type == TextureType::EMISSIVE;
}

// Textures sampled on surfaces get a mip chain: without one, distant and
// grazing surfaces alias (shimmering color, sparkling specular from normal
// maps). UI images and HDR sources keep a single level
static bool HasMips(TextureType type) {
  switch (type) {
    case TextureType::ALBEDO:
    case TextureType::NORMAL:
    case TextureType::ROUGHNESS:
    case TextureType::METALLIC:
    case TextureType::OCCLUSION:
    case TextureType::EMISSIVE:
    case TextureType::HEIGHT:
      return true;
    default:
      return false;
  }
}

// sRGB <-> linear for 8 bit channels
static float SRGBToLinear(uint8_t value) {
  static const std::array<float, 256> table = [] {
    std::array<float, 256> t{};
    for (int i = 0; i < 256; ++i) {
      const float c = static_cast<float>(i) / 255.0f;
      t[i] = c <= 0.04045f ? c / 12.92f
                           : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    return t;
  }();
  return table[value];
}

static uint8_t LinearToSRGB(float value) {
  const float c = value <= 0.0031308f
                      ? value * 12.92f
                      : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
  return static_cast<uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static uint8_t ToByte(float value) {
  return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// Every level of an RGBA8 image, level 0 first and tightly packed, as bgfx
// expects for a texture with mips. Each level box-filters the previous one:
//  - color (sRGB) textures are averaged in linear space, or they darken
//  - normal maps are averaged as vectors and renormalized
//  - alpha is averaged as is
// ponytail: averaged alpha thins alpha-masked foliage in the distance; scale
// alpha per level to keep coverage (Castano 2010) if it becomes visible
static std::vector<uint8_t> BuildMipChain(const Content::ImageData& image,
                                          TextureType type) {
  const bool srgb = IsSRGB(type);
  const bool normal = type == TextureType::NORMAL;

  std::vector<uint8_t> chain = image.pixels;
  size_t level_offset = 0;
  uint32_t width = image.width;
  uint32_t height = image.height;

  while (width > 1 || height > 1) {
    const uint32_t next_width = std::max(1u, width / 2);
    const uint32_t next_height = std::max(1u, height / 2);
    const size_t next_offset = chain.size();
    chain.resize(next_offset + size_t(next_width) * next_height * 4);

    // Indexing through the vector: resize above may have moved it
    const auto texel = [&](uint32_t x, uint32_t y) {
      x = std::min(x, width - 1);
      y = std::min(y, height - 1);
      return chain.data() + level_offset + (size_t(y) * width + x) * 4;
    };

    for (uint32_t y = 0; y < next_height; ++y) {
      for (uint32_t x = 0; x < next_width; ++x) {
        const uint8_t* samples[4] = {
            texel(x * 2, y * 2), texel(x * 2 + 1, y * 2),
            texel(x * 2, y * 2 + 1), texel(x * 2 + 1, y * 2 + 1)};

        float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (const uint8_t* s : samples) {
          for (int c = 0; c < 3; ++c) {
            const float v = srgb    ? SRGBToLinear(s[c])
                            : normal ? s[c] / 255.0f * 2.0f - 1.0f
                                     : s[c] / 255.0f;
            sum[c] += v * 0.25f;
          }
          sum[3] += s[3] / 255.0f * 0.25f;
        }

        uint8_t* out =
            chain.data() + next_offset + (size_t(y) * next_width + x) * 4;
        if (normal) {
          float length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] +
                                   sum[2] * sum[2]);
          if (length < 1e-6f) {
            sum[0] = 0.0f;
            sum[1] = 0.0f;
            sum[2] = 1.0f;
            length = 1.0f;
          }
          for (int c = 0; c < 3; ++c)
            out[c] = ToByte(sum[c] / length * 0.5f + 0.5f);
        } else {
          for (int c = 0; c < 3; ++c)
            out[c] = srgb ? LinearToSRGB(sum[c]) : ToByte(sum[c]);
        }
        out[3] = ToByte(sum[3]);
      }
    }

    level_offset = next_offset;
    width = next_width;
    height = next_height;
  }
  return chain;
}

// Private constructors used to create textures with existing GPU handles
Texture::Texture(bgfx::TextureHandle handle_param, uint16_t width_param,
                 uint16_t height_param, TextureType type_param)
    : handle_(handle_param),
      width_(width_param),
      height_(height_param),
      type_(type_param) {}

// Destructor
Texture::~Texture() {
  if (bgfx::isValid(handle_))
    bgfx::destroy(handle_);
}

// Move constructor transfers ownership of GPU resources
Texture::Texture(Texture&& other) noexcept {
  *this = std::move(other);
}

// Move assignment transfers ownership, release existing resources
Texture& Texture::operator=(Texture&& other) noexcept {
  if (this != &other) {
    if (bgfx::isValid(handle_))
      bgfx::destroy(handle_);
    handle_ = other.handle_;
    width_ = other.width_;
    height_ = other.height_;
    type_ = other.type_;
    other.handle_ = {bgfx::kInvalidHandle};
  }
  return *this;
}

// Uploads a decoded image to the GPU
TextureUpload Texture::Prepare(const Content::ImageData& image,
                               TextureType type) {
  TextureUpload upload;
  if (!image.Valid())
    return upload;

  const bool hdr = image.format == Content::ImageFormat::RGBA16F;
  upload.width = image.width;
  upload.height = image.height;
  upload.type = type;
  upload.format =
      hdr ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
  upload.srgb = IsSRGB(type) && !hdr;

  // Surface textures: full mip chain
  // TODO: offline mips (e.g. KTX/DDS) to skip generating them on load
  upload.mips = !hdr && HasMips(type);
  upload.data = upload.mips ? BuildMipChain(image, type) : image.pixels;
  return upload;
}

std::shared_ptr<Texture> Texture::Create(const TextureUpload& upload,
                                         uint64_t sampler_flags) {
  if (upload.data.empty())
    return {};

  uint64_t flags = sampler_flags | (upload.srgb ? BGFX_TEXTURE_SRGB : 0ULL);

  // Anisotropic filtering with mips, so grazing surfaces (floors, walls)
  // stay sharp
  if (upload.mips)
    flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;

  bgfx::TextureHandle handle = bgfx::createTexture2D(
      upload.width, upload.height, upload.mips, 1, upload.format, flags,
      bgfx::copy(upload.data.data(), uint32_t(upload.data.size())));

  if (!bgfx::isValid(handle)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "Texture2D: bgfx::createTexture2D failed");
    return {};
  }

  return std::shared_ptr<Texture>(
      new Texture(handle, upload.width, upload.height, upload.type));
}

std::shared_ptr<Texture> Texture::Create(const Content::ImageData& image,
                                         TextureType type,
                                         uint64_t sampler_flags) {
  return Create(Prepare(image, type), sampler_flags);
}

// Method to load 2D texture from disk, uploads it to GPU
std::shared_ptr<Texture> Texture::Load2D(
    const std::filesystem::path& file, TextureType type,
    uint64_t sampler_flags /* = BGFX_SAMPLER_NONE */) {
  const Content::ImageData image =
      Content::ImageLoader::LoadFile(file, type == TextureType::HDR);

  auto texture = Create(image, type, sampler_flags);
  if (texture) {
    Logger::getInstance().Log(LogLevel::Debug, "Texture2D: uploaded '" +
                                                   file.filename().string() +
                                                   '\'');
  }
  return texture;
}

// TODO: this cache belongs to the content layer: migrate its callers (icons,
// texture assets) to Content::ImageCache + TextureRegistry

// Return singleton instance of TextureCache
TextureCache& TextureCache::Instance() {
  static TextureCache cache;
  return cache;
}

// Get texture from cache or load it if not found
std::shared_ptr<Texture> TextureCache::Get(const std::filesystem::path& file,
                                           TextureType type) {
  const std::string key = file.string();

  {
    std::scoped_lock lock(mtx_);
    if (auto it = map_.find(key); it != map_.end())
      return it->second;
  }

  // If not found in cache, load texture from disk
  auto texture = Texture::Load2D(file, type);
  if (texture) {
    std::scoped_lock lock(mtx_);
    map_.emplace(key, texture);
  }
  return texture;
}

// Clear the texture cache
void TextureCache::Clear() {
  std::scoped_lock lock(mtx_);
  map_.clear();  // shared pointer, handle destruction of GPU resources
                 // TODO: attach to resource manager
}
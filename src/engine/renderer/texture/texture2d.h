#ifndef TEXTURE2D_H
#define TEXTURE2D_H

#include <bgfx/bgfx.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <array>

#include "engine/content/image_data.h"

// --------------------------------------------------------------------------
//  2D texture wrapper
//  -------------------------------------------------------------------------

// Define texture types
enum class TextureType {
  IMAGE,
  ALBEDO,
  NORMAL,
  ROUGHNESS,
  METALLIC,
  OCCLUSION,
  EMISSIVE,
  HEIGHT,
  HDR,   // float image (e.g. .hdr), kept linear in RGBA16F
  EMPTY  // uninitialised or invalid
};

// Manage GPU texture resources
class Texture {
public:
  Texture() = default;
  ~Texture();  // destroy GPU handle

  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&&) noexcept;
  Texture& operator=(Texture&&) noexcept;

  // Accessors for texture state
  [[nodiscard]] bool Valid() const { return bgfx::isValid(handle_); }
  [[nodiscard]] uint32_t Id() const { return handle_.idx; }
  [[nodiscard]] bgfx::TextureHandle Handle() const { return handle_; }

  // Upload a decoded image (color space and format follow the type)
  static std::shared_ptr<Texture> Create(
      const Content::ImageData& image, TextureType type,
      uint64_t sampler_flags = BGFX_SAMPLER_NONE);

  // load texture from file
  static std::shared_ptr<Texture> Load2D(
      const std::filesystem::path& file, TextureType type = TextureType::IMAGE,
      uint64_t sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

private:
  // constructor used by Create; Creates textures with existing GPU handles
  Texture(bgfx::TextureHandle handle_param, uint16_t width_param,
          uint16_t height_param, TextureType type_param);

  // Define texture properties and GPU resource handle
  bgfx::TextureHandle handle_{bgfx::kInvalidHandle};
  uint16_t width_ = 0;
  uint16_t height_ = 0;
  TextureType type_ = TextureType::EMPTY;
};

// Singleton, prevent duplicate texture loads
class TextureCache {
public:
  static TextureCache& Instance();

  // Retrieve cached texture; loads texture if not found
  std::shared_ptr<Texture> Get(const std::filesystem::path& file,
                               TextureType type = TextureType::IMAGE);

  void Clear();  // TODO: integrate with a resource manager

private:
  std::mutex mtx_;
  std::unordered_map<std::string, std::shared_ptr<Texture>> map_;
};

#endif  // TEXTURE2D_H
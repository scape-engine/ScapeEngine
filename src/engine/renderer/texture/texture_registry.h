#ifndef TEXTURE_REGISTRY_H
#define TEXTURE_REGISTRY_H

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "engine/renderer/texture/texture2d.h"

using TextureId = uint32_t;
constexpr TextureId INVALID_TEXTURE = 0;

// GPU textures by handle. Images come from Content::ImageCache (decoded
// once); each image is uploaded once per texture type, since the type decides
// the color space (e.g. the same file as sRGB albedo or linear mask)
class TextureRegistry {
public:
  static TextureRegistry& Instance();

  // Texture of an image cache key (file path or embedded key), uploaded on
  // first use. INVALID_TEXTURE if the key is empty or the image can't load
  TextureId Acquire(const std::string& key, TextureType type,
                    uint64_t sampler_flags = BGFX_SAMPLER_NONE);

  // GPU handle, invalid for unknown ids
  bgfx::TextureHandle Get(TextureId id) const;

  // TODO: ref counting and unloading, per-texture sampler state
  void Clear();

private:
  TextureRegistry() = default;

  mutable std::mutex mutex_;
  std::unordered_map<TextureId, std::shared_ptr<Texture>> textures_;
  std::unordered_map<std::string, TextureId> ids_;  // "type:key" -> id
  TextureId next_id_{1};
};

#endif  // TEXTURE_REGISTRY_H

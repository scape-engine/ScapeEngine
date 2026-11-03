#include "texture_registry.h"

#include "engine/content/cache/image_cache.h"
#include "engine/core/logger.h"

TextureRegistry& TextureRegistry::Instance() {
  static TextureRegistry instance;
  return instance;
}

TextureId TextureRegistry::Acquire(const std::string& key, TextureType type,
                                   uint64_t sampler_flags) {
  if (key.empty())
    return INVALID_TEXTURE;

  std::scoped_lock lock(mutex_);

  const std::string id_key = std::to_string(static_cast<int>(type)) + ":" + key;
  if (auto it = ids_.find(id_key); it != ids_.end())
    return it->second;

  auto texture = Texture::Create(Content::ImageCache::Instance().Get(key), type,
                                 sampler_flags);
  if (!texture) {
    // Remembered as missing, to avoid repeated upload attempts
    ids_[id_key] = INVALID_TEXTURE;
    return INVALID_TEXTURE;
  }

  const TextureId id = next_id_++;
  textures_[id] = std::move(texture);
  ids_[id_key] = id;

  Logger::getInstance().Log(
      LogLevel::Debug,
      "[TextureRegistry] Uploaded '" + key + "' with id " + std::to_string(id));
  return id;
}

bgfx::TextureHandle TextureRegistry::Get(TextureId id) const {
  std::scoped_lock lock(mutex_);

  auto it = textures_.find(id);
  if (it == textures_.end()) {
    bgfx::TextureHandle invalid = BGFX_INVALID_HANDLE;
    return invalid;
  }
  return it->second->Handle();
}

void TextureRegistry::Clear() {
  std::scoped_lock lock(mutex_);

  textures_.clear();  // shared pointers destroy the GPU handles
  ids_.clear();
  next_id_ = 1;
}

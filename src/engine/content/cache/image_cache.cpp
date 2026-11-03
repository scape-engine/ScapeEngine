#include "image_cache.h"

#include "engine/content/loaders/image_loader.h"

namespace Content {

ImageCache& ImageCache::Instance() {
  static ImageCache instance;
  return instance;
}

const ImageData& ImageCache::Get(const std::string& key) {
  auto it = cache_.find(key);
  if (it != cache_.end())
    return it->second;

  // Failed loads are cached too, to avoid repeated load attempts
  auto [emplace_it, _] = cache_.emplace(key, ImageLoader::LoadFile(key));
  return emplace_it->second;
}

void ImageCache::Put(const std::string& key, ImageData image) {
  cache_[key] = std::move(image);
}

bool ImageCache::Contains(const std::string& key) const {
  return cache_.find(key) != cache_.end();
}

void ImageCache::Evict(const std::string& key) {
  cache_.erase(key);
}

void ImageCache::Clear() {
  cache_.clear();
}

}  // namespace Content

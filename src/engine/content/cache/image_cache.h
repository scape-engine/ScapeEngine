#ifndef IMAGE_CACHE_H
#define IMAGE_CACHE_H

#include <mutex>
#include <string>
#include <unordered_map>

#include "engine/content/image_data.h"

namespace Content {

// Decoded images by key: a file path, or a generated key for images that
// only exist inside another file (e.g. "model.glb#*0" for embedded textures).
// Thread safe: models load on a worker thread. References stay valid until
// the image is evicted
class ImageCache {
public:
  static ImageCache& Instance();

  // Decodes the file on first use (LDR, RGBA8); invalid if it can't load
  const ImageData& Get(const std::string& key);

  // Stores an image decoded elsewhere, e.g. from an embedded texture
  void Put(const std::string& key, ImageData image);

  bool Contains(const std::string& key) const;

  // Cache management
  void Evict(const std::string& key);
  void Clear();
  size_t Size() const { return cache_.size(); }

private:
  ImageCache() = default;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ImageData> cache_;
};

}  // namespace Content

#endif  // IMAGE_CACHE_H

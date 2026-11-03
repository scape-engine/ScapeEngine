#ifndef IMAGE_CACHE_H
#define IMAGE_CACHE_H

#include <string>
#include <unordered_map>

#include "engine/content/image_data.h"

namespace Content {

// Decoded images by key: a file path, or a generated key for images that
// only exist inside another file (e.g. "model.glb#*0" for embedded textures)
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
  std::unordered_map<std::string, ImageData> cache_;
};

}  // namespace Content

#endif  // IMAGE_CACHE_H

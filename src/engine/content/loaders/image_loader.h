#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include <cstddef>
#include <filesystem>

#include "engine/content/image_data.h"

namespace Content {

// Decodes image files with stb_image (PNG, JPG, TGA, BMP, HDR, ...). Images
// are always expanded to 4 channels, so the renderer gets one layout
class ImageLoader {
public:
  // hdr: keep the full range as RGBA16F (e.g. .hdr). LDR files load too,
  // converted to linear floats by stb
  static ImageData LoadFile(const std::filesystem::path& path,
                            bool hdr = false);

  // Encoded image in memory, e.g. embedded in a .glb
  static ImageData LoadMemory(const void* data, size_t size);
};

}  // namespace Content

#endif  // IMAGE_LOADER_H

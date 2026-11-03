#ifndef IMAGE_DATA_H
#define IMAGE_DATA_H

#include <cstdint>
#include <vector>

//
// IMAGE_DATA_H
// Decoded image, CPU side: what the content layer hands to the renderer
//

namespace Content {

enum class ImageFormat {
  RGBA8,   // 8 bits per channel (PNG, JPG, TGA, ...)
  RGBA16F  // half floats, keeps the range of HDR images
};

struct ImageData {
  uint16_t width = 0;
  uint16_t height = 0;
  ImageFormat format = ImageFormat::RGBA8;
  std::vector<uint8_t> pixels;  // tightly packed rows, top row first

  bool Valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

}  // namespace Content

#endif  // IMAGE_DATA_H

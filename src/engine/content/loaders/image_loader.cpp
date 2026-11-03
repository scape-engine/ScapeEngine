#include "image_loader.h"

#include <bx/math.h>
#include <stb/stb_image.h>

#include <cstring>
#include <string>

#include "engine/core/logger.h"

namespace Content {

namespace {

// stb may be built without failure strings
std::string FailureReason() {
  const char* reason = stbi_failure_reason();
  return reason ? reason : "unknown error";
}

ImageData FromRGBA8(stbi_uc* pixels, int width, int height) {
  ImageData image;
  image.width = static_cast<uint16_t>(width);
  image.height = static_cast<uint16_t>(height);
  image.format = ImageFormat::RGBA8;

  const size_t size = size_t(width) * size_t(height) * 4;
  image.pixels.resize(size);
  memcpy(image.pixels.data(), pixels, size);
  return image;
}

// RGBA16F: filterable on every backend, unlike RGBA32F
ImageData FromRGBA32F(float* pixels, int width, int height) {
  ImageData image;
  image.width = static_cast<uint16_t>(width);
  image.height = static_cast<uint16_t>(height);
  image.format = ImageFormat::RGBA16F;

  const size_t count = size_t(width) * size_t(height) * 4;
  image.pixels.resize(count * sizeof(uint16_t));
  uint16_t* halfs = reinterpret_cast<uint16_t*>(image.pixels.data());
  for (size_t i = 0; i < count; ++i) {
    halfs[i] = bx::halfFromFloat(pixels[i]);
  }
  return image;
}

}  // namespace

ImageData ImageLoader::LoadFile(const std::filesystem::path& path, bool hdr) {
  int width{}, height{}, channels{};
  const std::string file = path.string();

  ImageData image;
  if (hdr) {
    float* pixels = stbi_loadf(file.c_str(), &width, &height, &channels, 4);
    if (pixels) {
      image = FromRGBA32F(pixels, width, height);
      stbi_image_free(pixels);
    }
  } else {
    stbi_uc* pixels = stbi_load(file.c_str(), &width, &height, &channels, 4);
    if (pixels) {
      image = FromRGBA8(pixels, width, height);
      stbi_image_free(pixels);
    }
  }

  if (!image.Valid()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ImageLoader] Failed to load '" + file +
                                  "': " + FailureReason());
  }
  return image;
}

ImageData ImageLoader::LoadMemory(const void* data, size_t size) {
  int width{}, height{}, channels{};
  stbi_uc* pixels =
      stbi_load_from_memory(static_cast<const stbi_uc*>(data), int(size),
                            &width, &height, &channels, 4);
  if (!pixels) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[ImageLoader] Failed to decode image: " + FailureReason());
    return {};
  }

  ImageData image = FromRGBA8(pixels, width, height);
  stbi_image_free(pixels);
  return image;
}

}  // namespace Content

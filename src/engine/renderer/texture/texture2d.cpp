#include "texture2d.h"
#include "engine/core/logger.h"

#include "engine/content/loaders/image_loader.h"

// TODO: texture2d should be using EngineContext

// Color textures are authored in sRGB and must be linearized when sampled
// (glTF 2.0 spec, Materials: base color and emissive textures are sRGB)
static bool IsSRGB(TextureType type) {
  return type == TextureType::ALBEDO || type == TextureType::EMISSIVE;
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
std::shared_ptr<Texture> Texture::Create(const Content::ImageData& image,
                                         TextureType type,
                                         uint64_t sampler_flags) {
  if (!image.Valid())
    return {};

  const bool hdr = image.format == Content::ImageFormat::RGBA16F;
  const bgfx::TextureFormat::Enum format =
      hdr ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
  const uint64_t flags =
      sampler_flags | (IsSRGB(type) && !hdr ? BGFX_TEXTURE_SRGB : 0ULL);

  // TODO: mipmaps (generated on the CPU or offline), anisotropic filtering
  bgfx::TextureHandle handle = bgfx::createTexture2D(
      image.width, image.height, false, 1, format, flags,
      bgfx::copy(image.pixels.data(), uint32_t(image.pixels.size())));

  if (!bgfx::isValid(handle)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "Texture2D: bgfx::createTexture2D failed");
    return {};
  }

  return std::shared_ptr<Texture>(
      new Texture(handle, image.width, image.height, type));
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
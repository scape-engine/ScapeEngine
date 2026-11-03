#include "texture2d.h"
#include "engine/core/logger.h"

#include <bx/math.h>
#include <stb/stb_image.h>
#include <unordered_map>
#include <array>
#include <algorithm>

// TODO: texture2d should be using EngineContext

// ----------------------------------------------------------------------
// Format information for texture configuration
// ----------------------------------------------------------------------
struct FormatInfo {
  bgfx::TextureFormat::Enum fmt;
  bool srgb;  // Whether texture needs sRGB color space conversion
};

static constexpr std::array<FormatInfo, 3> kLinearByChannels = {
    {/* 1 ch */ {bgfx::TextureFormat::R8, false},
     /* 3 ch */ {bgfx::TextureFormat::RGB8, false},
     /* 4 ch */ {bgfx::TextureFormat::RGBA8, false}}};

// Format mapping for different texture types with appropriate color spaces
static const std::unordered_map<TextureType, FormatInfo> kTypeTable = {
    {TextureType::ALBEDO, {bgfx::TextureFormat::RGBA8, true}},    // sRGB
    {TextureType::EMISSIVE, {bgfx::TextureFormat::RGBA8, true}},  // sRGB
    {TextureType::NORMAL, {bgfx::TextureFormat::RGBA8, false}},
    {TextureType::ROUGHNESS, {bgfx::TextureFormat::R8, false}},
    {TextureType::METALLIC, {bgfx::TextureFormat::R8, false}},
    {TextureType::OCCLUSION, {bgfx::TextureFormat::R8, false}},
    {TextureType::HEIGHT, {bgfx::TextureFormat::R8, false}},
    {TextureType::IMAGE,
     {bgfx::TextureFormat::RGBA8, false}},  // Regular image display
    {TextureType::EMPTY, {bgfx::TextureFormat::RGBA8, false}}  // default
};

// ----------------------------------------------------------------------
// Selects appropriate texture format based on texture type and channel count
// ----------------------------------------------------------------------
[[nodiscard]]
FormatInfo ChooseFormat(TextureType type, int channels) {
  auto it = kTypeTable.find(type);
  FormatInfo info = (it != kTypeTable.end())
                        ? it->second
                        : kLinearByChannels[2];  // fallback RGBA8

  // For generic IMAGE type, choose format based on channel count
  if (type == TextureType::IMAGE) {
    channels = std::clamp(channels, 1, 4);
    info = kLinearByChannels[(channels == 1) ? 0
                             : (channels == 2)
                                 ? 0
                                 :  // Use R8 for both 1 and 2 channels
                                 (channels == 3) ? 1
                                                 : 2];
  }
  return info;
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

// Float images keep their full range (e.g. a bright sun in an HDRI). LDR
// files load too, stb converts them to linear
std::shared_ptr<Texture> Texture::LoadHDR(const std::filesystem::path& file,
                                          uint64_t sampler_flags) {
  int width{}, height{}, channels{};
  float* pixels =
      stbi_loadf(file.string().c_str(), &width, &height, &channels, 4);
  if (!pixels) {
    Logger::getInstance().Log(
        LogLevel::Error, "Texture2D: failed to load '" + file.string() + '\'');
    return {};
  }

  // RGBA16F: filterable on every backend, unlike RGBA32F
  const uint32_t count = uint32_t(width) * uint32_t(height) * 4;
  const bgfx::Memory* memory = bgfx::alloc(count * sizeof(uint16_t));
  uint16_t* halfs = reinterpret_cast<uint16_t*>(memory->data);
  for (uint32_t i = 0; i < count; ++i) {
    halfs[i] = bx::halfFromFloat(pixels[i]);
  }
  stbi_image_free(pixels);

  bgfx::TextureHandle handle = bgfx::createTexture2D(
      uint16_t(width), uint16_t(height), false, 1,
      bgfx::TextureFormat::RGBA16F, sampler_flags, memory);

  if (!bgfx::isValid(handle)) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "bgfx::createTexture2D failed for '" + file.string() + '\'');
    return {};
  }

  Logger::getInstance().Log(LogLevel::Debug, "Texture2D: uploaded HDR '" +
                                                 file.filename().string() +
                                                 '\'');

  return std::shared_ptr<Texture>(new Texture(
      handle, uint16_t(width), uint16_t(height), TextureType::HDR));
}

// Method to load 2D texture from disk, uploads it to GPU
std::shared_ptr<Texture> Texture::Load2D(
    const std::filesystem::path& file, TextureType type,
    uint64_t sampler_flags /* = BGFX_SAMPLER_NONE */) {

  if (type == TextureType::HDR)
    return LoadHDR(file, sampler_flags);

  // Load image data using from disk using stb_image
  int width{}, height{}, channels{};
  stbi_uc* pixels =
      stbi_load(file.string().c_str(), &width, &height, &channels, 0);
  if (!pixels) {
    Logger::getInstance().Log(
        LogLevel::Error, "Texture2D: failed to load '" + file.string() + '\'');
    return {};
  }

  // Copy pixel data to bgfx memory for GPU upload
  const bgfx::Memory* memory = bgfx::copy(pixels, width * height * 4);
  stbi_image_free(pixels);

  // Select appropriate format and flags based on texture type
  const auto fmt_info = ChooseFormat(type, channels);
  const uint64_t flags =
      sampler_flags | (fmt_info.srgb ? BGFX_TEXTURE_SRGB : 0ULL);

  // Create GPU texture with selected format and flags
  bgfx::TextureHandle handle = bgfx::createTexture2D(
      uint16_t(width), uint16_t(height), false, 1, fmt_info.fmt, flags, memory);

  if (!bgfx::isValid(handle)) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "bgfx::createTexture2D failed for '" + file.string() + '\'');
    return {};
  }

  // Manage the GPU handle with a created texture object
  auto texture = std::shared_ptr<Texture>(
      new Texture(handle, static_cast<uint16_t>(width),
                  static_cast<uint16_t>(height), type));

  Logger::getInstance().Log(
      LogLevel::Debug, "Texture2D: uploaded '" + file.filename().string() +
                           "' (fmt= " + std::to_string(fmt_info.fmt) + ')');

  return texture;
}

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
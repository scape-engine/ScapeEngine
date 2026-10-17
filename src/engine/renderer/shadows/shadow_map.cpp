#include "shadow_map.h"
#include "engine/core/logger.h"

ShadowMap::ShadowMap(uint16_t resolution)
    : resolution_(resolution),
      depth_texture_(BGFX_INVALID_HANDLE),
      framebuffer_(BGFX_INVALID_HANDLE),
      light_space_matrix_(1.0f) {}

ShadowMap::~ShadowMap() {
  Destroy();
}

void ShadowMap::Create() {
  // Create depth texture
  depth_texture_ = bgfx::createTexture2D(
      resolution_, resolution_,
      false,                                       // No mips
      1,                                           // Single layer
      bgfx::TextureFormat::D32F,                   // 32-bit depth
      BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LESS  // Render target + PCF
  );

  if (!bgfx::isValid(depth_texture_)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "ShadowMap: Failed to create depth texture");
    return;
  }

  // Create framebuffer with depth attachment
  framebuffer_ = bgfx::createFrameBuffer(1, &depth_texture_, true);

  if (!bgfx::isValid(framebuffer_)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "ShadowMap: Failed to create framebuffer");
    bgfx::destroy(depth_texture_);
    depth_texture_ = BGFX_INVALID_HANDLE;
    return;
  }

  // Create shadow uniforms
  u_light_matrix_ =
      bgfx::createUniform("u_lightMatrix", bgfx::UniformType::Mat4);
  s_shadow_map_ =
      bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);

  Logger::getInstance().Log(
      LogLevel::Info, "ShadowMap: Created " + std::to_string(resolution_) +
                          "x" + std::to_string(resolution_) + " shadow map");
}

void ShadowMap::Destroy() {
  if (bgfx::isValid(u_light_matrix_)) {
    bgfx::destroy(u_light_matrix_);
    u_light_matrix_ = BGFX_INVALID_HANDLE;
  }

  if (bgfx::isValid(s_shadow_map_)) {
    bgfx::destroy(s_shadow_map_);
    s_shadow_map_ = BGFX_INVALID_HANDLE;
  }

  if (bgfx::isValid(framebuffer_)) {
    // Created with destroyTextures=true: this also destroys depth_texture_
    bgfx::destroy(framebuffer_);
    framebuffer_ = BGFX_INVALID_HANDLE;
    depth_texture_ = BGFX_INVALID_HANDLE;
  }

  // Only reached if the framebuffer was never created
  if (bgfx::isValid(depth_texture_)) {
    bgfx::destroy(depth_texture_);
    depth_texture_ = BGFX_INVALID_HANDLE;
  }

  light_space_matrix_ = glm::mat4(1.0f);
}
#include "pre_pass.h"

#include <glm/gtc/type_ptr.hpp>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/core/view_ids.h"
#include "engine/ecs/world.h"
#include "engine/renderer/model/mesh.h"

PrePass::PrePass()
    : program_(BGFX_INVALID_HANDLE),
      fbo_(BGFX_INVALID_HANDLE),
      normal_output_(BGFX_INVALID_HANDLE),
      depth_output_(BGFX_INVALID_HANDLE),
      target_width_(0),
      target_height_(0) {}

void PrePass::Create() {
  program_ = EngineContext::Shader().LoadProgram("pre_pass", "vs_pre_pass.bin",
                                                 "fs_pre_pass.bin");

  if (!bgfx::isValid(program_)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "PrePass: Failed to load shaders");
  }
}

void PrePass::Destroy() {
  DestroyTargets();
}

void PrePass::Render(const glm::mat4& view, const glm::mat4& projection,
                     uint16_t width, uint16_t height, uint16_t target_width,
                     uint16_t target_height) {
  if (!bgfx::isValid(program_) || width == 0 || height == 0)
    return;

  // Follow the scene framebuffer size
  if (target_width != target_width_ || target_height != target_height_) {
    DestroyTargets();
    CreateTargets(target_width, target_height);
  }

  if (!bgfx::isValid(fbo_))
    return;

  // Cleared to 0: zero depth marks the background
  bgfx::setViewRect(ViewID::PRE_PASS, 0, 0, width, height);
  bgfx::setViewFrameBuffer(ViewID::PRE_PASS, fbo_);
  bgfx::setViewClear(ViewID::PRE_PASS, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                     0x00000000, 1.0f, 0);
  bgfx::setViewTransform(ViewID::PRE_PASS, glm::value_ptr(view),
                         glm::value_ptr(projection));
  bgfx::touch(ViewID::PRE_PASS);

  // Render every mesh (transforms must have been evaluated beforehand)
  for (auto [entity, transform, renderer] : ECS::Main().GetRenderQueue()) {
    if (!renderer.enabled_ || !renderer.mesh_)
      continue;

    bgfx::setTransform(glm::value_ptr(transform.model_));
    renderer.mesh_->Bind();

    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                   BGFX_STATE_CULL_CW);
    bgfx::submit(ViewID::PRE_PASS, program_);
  }
}

bgfx::TextureHandle PrePass::GetNormalOutput() const {
  return normal_output_;
}

bgfx::TextureHandle PrePass::GetDepthOutput() const {
  return depth_output_;
}

void PrePass::CreateTargets(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0)
    return;

  const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_POINT |
                         BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

  normal_output_ = bgfx::createTexture2D(
      width, height, false, 1, bgfx::TextureFormat::RGBA16F, flags);
  depth_output_ = bgfx::createTexture2D(width, height, false, 1,
                                        bgfx::TextureFormat::R32F, flags);
  bgfx::TextureHandle depth_buffer =
      bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D24S8,
                            BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);

  // Destroys its textures with it
  const bgfx::TextureHandle attachments[3] = {normal_output_, depth_output_,
                                              depth_buffer};
  fbo_ = bgfx::createFrameBuffer(3, attachments, true);

  if (!bgfx::isValid(fbo_)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "PrePass: Failed to create framebuffer");
  }

  target_width_ = width;
  target_height_ = height;
}

void PrePass::DestroyTargets() {
  if (bgfx::isValid(fbo_))
    bgfx::destroy(fbo_);

  fbo_ = BGFX_INVALID_HANDLE;
  normal_output_ = BGFX_INVALID_HANDLE;
  depth_output_ = BGFX_INVALID_HANDLE;
  target_width_ = 0;
  target_height_ = 0;
}

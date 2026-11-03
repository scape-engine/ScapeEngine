#include "ssao_pass.h"

#include <algorithm>
#include <random>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/core/view_ids.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/primitives/global_quad.h"

SSAOPass::SSAOPass()
    : ao_program_(BGFX_INVALID_HANDLE),
      blur_program_(BGFX_INVALID_HANDLE),
      kernel_(),
      noise_texture_(BGFX_INVALID_HANDLE),
      ao_fbo_(BGFX_INVALID_HANDLE),
      blur_fbo_(BGFX_INVALID_HANDLE),
      target_width_(0),
      target_height_(0) {}

void SSAOPass::Create() {
  ao_program_ = EngineContext::Shader().LoadProgram("ssao", "vs_ssao.bin",
                                                    "fs_ssao.bin");
  blur_program_ = EngineContext::Shader().LoadProgram(
      "ssao_blur", "vs_ssao.bin", "fs_ssao_blur.bin");

  if (!bgfx::isValid(ao_program_) || !bgfx::isValid(blur_program_)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "SSAOPass: Failed to load shaders");
  }

  // Sample kernel and noise texture
  kernel_ = GenerateKernel();
  noise_texture_ = GenerateNoiseTexture();
}

void SSAOPass::Destroy() {
  DestroyTargets();

  if (bgfx::isValid(noise_texture_))
    bgfx::destroy(noise_texture_);
  noise_texture_ = BGFX_INVALID_HANDLE;

  kernel_.clear();
}

void SSAOPass::Render(const AmbientOcclusionSettings& settings,
                      const glm::mat4& projection,
                      bgfx::TextureHandle normal_input,
                      bgfx::TextureHandle depth_input, uint16_t width,
                      uint16_t height, uint16_t target_width,
                      uint16_t target_height) {
  if (!bgfx::isValid(ao_program_) || !bgfx::isValid(blur_program_) ||
      !bgfx::isValid(normal_input) || !bgfx::isValid(depth_input))
    return;

  // Follow the pre-pass size
  if (target_width != target_width_ || target_height != target_height_) {
    DestroyTargets();
    CreateTargets(target_width, target_height);
  }

  if (!bgfx::isValid(ao_fbo_) || !bgfx::isValid(blur_fbo_))
    return;

  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());

  // Get current sample amount
  const int32_t samples =
      std::clamp(settings.samples, 1, int32_t(kernel_.size()));

  const float params[4] = {float(samples), settings.radius, settings.bias,
                           settings.power};
  const float projection_params[4] = {projection[0][0], projection[1][1], 0.0f,
                                      0.0f};
  // NDC y grows with the pixel row only on bottom left origin backends
  const float screen[4] = {1.0f / float(target_width_),
                           1.0f / float(target_height_),
                           bgfx::getCaps()->originBottomLeft ? 1.0f : -1.0f,
                           1.0f / float(noise_resolution_)};
  const uint32_t point_clamp =
      BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

  // Ambient occlusion pass
  bgfx::setViewRect(ViewID::SSAO, 0, 0, width, height);
  bgfx::setViewFrameBuffer(ViewID::SSAO, ao_fbo_);
  bgfx::setUniform(graphics.GetSSAOKernelUniform(), kernel_.data(),
                   uint16_t(kernel_.size()));
  bgfx::setUniform(graphics.GetSSAOParamsUniform(), params);
  bgfx::setUniform(graphics.GetSSAOProjectionUniform(), projection_params);
  bgfx::setUniform(graphics.GetSSAOScreenUniform(), screen);
  bgfx::setTexture(0, graphics.GetSSAONormalsSamplerUniform(), normal_input,
                   point_clamp);
  bgfx::setTexture(1, graphics.GetSSAODepthSamplerUniform(), depth_input,
                   point_clamp);
  bgfx::setTexture(2, graphics.GetSSAONoiseSamplerUniform(), noise_texture_);
  GlobalQuad::Submit(ViewID::SSAO, ao_program_);

  // Blur pass: separate target, it can't read the one it writes
  bgfx::setViewRect(ViewID::SSAO_BLUR, 0, 0, width, height);
  bgfx::setViewFrameBuffer(ViewID::SSAO_BLUR, blur_fbo_);
  bgfx::setUniform(graphics.GetSSAOScreenUniform(), screen);
  bgfx::setTexture(0, graphics.GetSSAOInputSamplerUniform(),
                   bgfx::getTexture(ao_fbo_), point_clamp);
  GlobalQuad::Submit(ViewID::SSAO_BLUR, blur_program_);
}

bgfx::TextureHandle SSAOPass::GetOutput() const {
  if (!bgfx::isValid(blur_fbo_)) {
    bgfx::TextureHandle invalid = BGFX_INVALID_HANDLE;
    return invalid;
  }
  return bgfx::getTexture(blur_fbo_);
}

std::vector<glm::vec4> SSAOPass::GenerateKernel() {
  // One seeded generator: the kernel is the same every run
  std::mt19937 generator(1337u);
  std::uniform_real_distribution<float> random(0.0f, 1.0f);

  const uint16_t size = GraphicsRenderer::kSSAOKernelSize;
  std::vector<glm::vec4> kernel;
  kernel.reserve(size);

  for (uint16_t i = 0; i < size; ++i) {
    // Random direction in the hemisphere around +z (the normal)
    glm::vec3 sample(random(generator) * 2.0f - 1.0f,
                     random(generator) * 2.0f - 1.0f, random(generator));
    sample = glm::normalize(sample) * random(generator);

    // More weight to samples closer to the center
    float scale = float(i) / float(size);
    sample *= glm::mix(0.1f, 1.0f, scale * scale);

    kernel.emplace_back(sample, 0.0f);
  }

  return kernel;
}

bgfx::TextureHandle SSAOPass::GenerateNoiseTexture() {
  std::mt19937 generator(7331u);
  std::uniform_int_distribution<uint32_t> random(0, 255);

  // Random rotation vectors in the tangent plane (xy), z = 0 (stored as 0.5)
  const uint32_t count = noise_resolution_ * noise_resolution_;
  const bgfx::Memory* memory = bgfx::alloc(count * 4);
  for (uint32_t i = 0; i < count; ++i) {
    memory->data[i * 4 + 0] = uint8_t(random(generator));
    memory->data[i * 4 + 1] = uint8_t(random(generator));
    memory->data[i * 4 + 2] = 128;
    memory->data[i * 4 + 3] = 255;
  }

  // Tiled across the screen, read per pixel
  return bgfx::createTexture2D(noise_resolution_, noise_resolution_, false, 1,
                               bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_POINT,
                               memory);
}

void SSAOPass::CreateTargets(uint16_t width, uint16_t height) {
  if (width == 0 || height == 0)
    return;

  const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                         BGFX_SAMPLER_V_CLAMP;
  ao_fbo_ =
      bgfx::createFrameBuffer(width, height, bgfx::TextureFormat::R8, flags);
  blur_fbo_ =
      bgfx::createFrameBuffer(width, height, bgfx::TextureFormat::R8, flags);

  if (!bgfx::isValid(ao_fbo_) || !bgfx::isValid(blur_fbo_)) {
    Logger::getInstance().Log(LogLevel::Error,
                              "SSAOPass: Failed to create framebuffers");
  }

  target_width_ = width;
  target_height_ = height;
}

void SSAOPass::DestroyTargets() {
  // Framebuffers destroy their own textures
  if (bgfx::isValid(ao_fbo_))
    bgfx::destroy(ao_fbo_);
  if (bgfx::isValid(blur_fbo_))
    bgfx::destroy(blur_fbo_);

  ao_fbo_ = BGFX_INVALID_HANDLE;
  blur_fbo_ = BGFX_INVALID_HANDLE;
  target_width_ = 0;
  target_height_ = 0;
}

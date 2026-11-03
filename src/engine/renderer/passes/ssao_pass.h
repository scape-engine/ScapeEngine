#ifndef SSAO_PASS_H
#define SSAO_PASS_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <vector>

// Screen space ambient occlusion settings
struct AmbientOcclusionSettings {
  bool enabled = true;
  int32_t samples = 16;   // kernel samples per pixel (up to the kernel size)
  float radius = 0.5f;    // world units around each pixel
  float bias = 0.025f;    // avoids self occlusion on flat surfaces
  float power = 1.0f;     // contrast of the result
};

// Screen space ambient occlusion from the pre-pass outputs: hemisphere kernel
// around each pixel's normal, then a blur that removes the noise pattern
class SSAOPass {
public:
  SSAOPass();

  void Create();
  void Destroy();

  // AO of the top left width x height area. Targets are target_width x
  // target_height, the pre-pass size, and are recreated when it changes
  void Render(const AmbientOcclusionSettings& settings,
              const glm::mat4& projection, bgfx::TextureHandle normal_input,
              bgfx::TextureHandle depth_input, uint16_t width, uint16_t height,
              uint16_t target_width, uint16_t target_height);

  // Blurred ambient occlusion (R8), 1 = unoccluded
  bgfx::TextureHandle GetOutput() const;

private:
  std::vector<glm::vec4> GenerateKernel();
  bgfx::TextureHandle GenerateNoiseTexture();

  void CreateTargets(uint16_t width, uint16_t height);
  void DestroyTargets();

  // Tiled random rotations, one per pixel of a noise_resolution_ square
  static constexpr uint16_t noise_resolution_ = 4;

  bgfx::ProgramHandle ao_program_;
  bgfx::ProgramHandle blur_program_;

  std::vector<glm::vec4> kernel_;
  bgfx::TextureHandle noise_texture_;

  bgfx::FrameBufferHandle ao_fbo_;
  bgfx::FrameBufferHandle blur_fbo_;
  uint16_t target_width_;
  uint16_t target_height_;
};

#endif  // SSAO_PASS_H

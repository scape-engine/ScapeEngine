#ifndef PRE_PASS_H
#define PRE_PASS_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

// Geometry pass before the forward pass: view-space normals and linear view
// depth of every mesh, the inputs of screen-space effects (SSAO)
class PrePass {
public:
  PrePass();

  void Create();
  void Destroy();

  // Renders the ECS render queue into the top left width x height area.
  // Targets are target_width x target_height, the scene framebuffer size, and
  // are recreated when it changes
  void Render(const glm::mat4& view, const glm::mat4& projection,
              uint16_t width, uint16_t height, uint16_t target_width,
              uint16_t target_height);

  // View-space normals (RGBA16F)
  bgfx::TextureHandle GetNormalOutput() const;

  // Linear view depth (R32F), 0 where nothing was drawn
  bgfx::TextureHandle GetDepthOutput() const;

private:
  void CreateTargets(uint16_t width, uint16_t height);
  void DestroyTargets();

  bgfx::ProgramHandle program_;

  bgfx::FrameBufferHandle fbo_;
  bgfx::TextureHandle normal_output_;
  bgfx::TextureHandle depth_output_;
  uint16_t target_width_;
  uint16_t target_height_;
};

#endif  // PRE_PASS_H

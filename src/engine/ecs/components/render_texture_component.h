#ifndef RENDER_TEXTURE_COMPONENT_H
#define RENDER_TEXTURE_COMPONENT_H

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <vector>

#include "engine/renderer/texture/texture_registry.h"

// Textures an entity's compute jobs write and its draws sample, made by the
// compute system when missing and released when the component goes (on_destroy
// hook). Runtime only: not saved, the system that fills them adds them again
struct RenderTextureComponent {
  struct Texture {
    std::string name;  // the sampler (draws) and image (compute) uniform
    uint16_t width = 256;
    uint16_t height = 256;
    uint16_t layers = 1;  // more than one: a texture array
    bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA16F;
    TextureId id = INVALID_TEXTURE;  // kept by the compute system
  };
  std::vector<Texture> textures_;

  // Move-only, so a duplicated entity never shares (and releases) the ids
  RenderTextureComponent() = default;
  RenderTextureComponent(RenderTextureComponent&&) = default;
  RenderTextureComponent& operator=(RenderTextureComponent&&) = default;
  RenderTextureComponent(const RenderTextureComponent&) = delete;
  RenderTextureComponent& operator=(const RenderTextureComponent&) = delete;
};

#endif  // RENDER_TEXTURE_COMPONENT_H

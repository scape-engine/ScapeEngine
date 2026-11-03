#ifndef MATERIAL_H
#define MATERIAL_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "engine/renderer/texture/texture_registry.h"

// A texture a material binds to one stage of its program
struct MaterialTexture {
  bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
  uint8_t stage = 0;
  TextureId texture = INVALID_TEXTURE;

  // Bound while the texture is unset or failed to load, so the sampler is
  // never left unbound (e.g. white for color/masks, flat normal for normals)
  bgfx::TextureHandle fallback = BGFX_INVALID_HANDLE;
};

// A vec4 uniform value of a material
struct MaterialParam {
  bgfx::UniformHandle uniform = BGFX_INVALID_HANDLE;
  glm::vec4 value{0.0f};
};

// What a draw needs from its material: a program and the values it reads.
// Shading is entirely up to the program, so any shader can be used as a
// material (see StandardMaterial for the default PBR one)
struct Material {
  std::string name;
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  std::vector<MaterialTexture> textures;  // stages below MATERIAL_STAGE_COUNT
  std::vector<MaterialParam> params;

  // TODO: render state (blend/alpha mode, culling for double-sided)
};

#endif  // MATERIAL_H

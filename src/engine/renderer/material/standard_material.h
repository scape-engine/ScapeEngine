#ifndef STANDARD_MATERIAL_H
#define STANDARD_MATERIAL_H

#include "engine/core/types/material_data.h"
#include "engine/renderer/material/material.h"

#include <string>
#include <utility>
#include <vector>

// Metallic-roughness PBR material, the glTF 2.0 material model (glTF 2.0
// spec, Materials), shaded by vs_lit / fs_lit:
//  - base color = factor * texture (sRGB)
//  - roughness = factor * texture.g, metallic = factor * texture.b
//  - normal map (tangent space), occlusion map (texture.r, ambient only)
//  - emissive = factor * texture (sRGB)
//  - alpha mask (cutoff on base color alpha) and double-sided
class StandardMaterial {
public:
  static Material Build(const Content::MaterialData& data);

  // Texture keys and types the material samples, by stage (0-4)
  static std::vector<std::pair<std::string, TextureType>> Textures(
      const Content::MaterialData& data);
};

#endif  // STANDARD_MATERIAL_H

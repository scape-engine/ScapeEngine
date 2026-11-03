#ifndef STANDARD_MATERIAL_H
#define STANDARD_MATERIAL_H

#include "engine/core/types/material_data.h"
#include "engine/renderer/material/material.h"

// Metallic-roughness PBR material, the glTF 2.0 material model (glTF 2.0
// spec, Materials), shaded by vs_lit / fs_lit:
//  - base color = factor * texture (sRGB)
//  - roughness = factor * texture.g, metallic = factor * texture.b
//  - normal map (tangent space), occlusion map (texture.r, ambient only)
//  - emissive = factor * texture (sRGB)
class StandardMaterial {
public:
  static Material Build(const Content::MaterialData& data);
};

#endif  // STANDARD_MATERIAL_H

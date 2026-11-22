#ifndef MATERIAL_DATA_H
#define MATERIAL_DATA_H

#include <glm/glm.hpp>
#include <string>

namespace Content {

// How a surface covers what is behind it (glTF alphaMode): Mask cuts it away
// where alpha is under the cutoff, Blend mixes it in by alpha, Additive adds
// its colour (glTF has no additive mode: the loader reads an unlit Blend
// material as one, e.g. an engine glow)
enum class MaterialBlend { Opaque, Mask, Blend, Additive };

struct MaterialData {
  std::string name;
  bool is_pbr{false};

  // PBR properties
  glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
  float metallic{0.0f};
  float roughness{0.5f};

  // Textures, as Content::ImageCache keys: an absolute file path, or
  // "<model path>#<reference>" for images embedded in the model file
  std::string albedo_map;
  std::string normal_map;
  std::string metallic_roughness_map;  // glTF: roughness in G, metallic in B
  std::string ao_map;                  // occlusion in R
  std::string emissive_map;

  // Emission
  glm::vec3 emissive{0.0f};
  float emissive_strength{1.0f};

  MaterialBlend blend{MaterialBlend::Opaque};
  float alpha_cutoff{0.5f};  // Mask
  bool unlit{false};         // the base colour as it is, no lighting
};

}  // namespace Content

#endif  // MATERIAL_DATA_H
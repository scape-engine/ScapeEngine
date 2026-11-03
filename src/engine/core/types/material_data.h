#ifndef MATERIAL_DATA_H
#define MATERIAL_DATA_H

#include <glm/glm.hpp>
#include <string>

namespace Content {

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
};

}  // namespace Content

#endif  // MATERIAL_DATA_H
#ifndef MATERIAL_LOADER_H
#define MATERIAL_LOADER_H

#include <string>
#include <vector>

#include "engine/core/types/material_data.h"

// Forward declare
struct aiScene;

namespace Content {

class MaterialLoader {
public:
  // Load materials from a scene file (GLTF, FBX, OBJ, etc.)
  static std::vector<MaterialData> LoadFromScene(const std::string& path);

  // Materials of a scene that is already imported, so a model is parsed
  // once for both its meshes and its materials
  static std::vector<MaterialData> ExtractMaterials(
      const aiScene* scene, const std::string& model_path);
};

}  // namespace Content

#endif  // MATERIAL_LOADER_H
#ifndef MESH_LOADER_H
#define MESH_LOADER_H

#include <string>
#include <vector>

#include "engine/core/types/geometry_data.h"
#include "engine/core/types/material_data.h"
#include "engine/content/scene_data.h"

namespace Content {

class MeshLoader {
public:
  // Load from file (dispatch based on extension)
  // Materials are extracted from the same import when requested, instead of
  // parsing the file a second time
  static std::vector<Geometry::MeshData> Load(
      const std::string& path,
      std::vector<MaterialData>* materials = nullptr);

  static bool IsSupported(const std::string& path);

  // Load hierarchical scene files
  static SceneData LoadScene(const std::string& path);

};

}  // namespace Content

#endif  // MESH_LOADER_H

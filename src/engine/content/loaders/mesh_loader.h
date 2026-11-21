#ifndef MESH_LOADER_H
#define MESH_LOADER_H

#include <string>
#include <vector>

#include "engine/core/types/geometry_data.h"
#include "engine/content/scene_data.h"

namespace Content {

class MeshLoader {
public:
  // Load from file (dispatch based on extension). Given `skeletal`, an
  // animated model fills it and its meshes come skinned to it, in model space
  static std::vector<Geometry::MeshData> Load(const std::string& path,
                                              SkeletalData* skeletal = nullptr);

  static bool IsSupported(const std::string& path);

  // Load hierarchical scene files
  static SceneData LoadScene(const std::string& path);

};

}  // namespace Content

#endif  // MESH_LOADER_H

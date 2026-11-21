#ifndef SCENE_DATA_H
#define SCENE_DATA_H

#include <glm/mat4x4.hpp>
#include <string>
#include <vector>

#include "engine/core/types/geometry_data.h"
#include "engine/core/types/material_data.h"
#include "engine/animation/skeleton.h"

//
// SCENE_DATA_H
// Data container for hierarchal imports
//

namespace Content {

struct SceneNode {
  std::string name;
  glm::mat4 local_transform;
  std::vector<int> mesh_indices;
  std::vector<SceneNode> children;
};

struct SceneData {
  SceneNode root;
  std::vector<Geometry::MeshData> mesh_data;
  std::vector<Content::MaterialData> materials;
  // Every node, which skinned meshes' bones name, and the animations
  SkeletalData skeletal;
};

}  // namespace Content

#endif  // SCENE_DATA_H

#ifndef MODEL_GRAPH_H
#define MODEL_GRAPH_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/types/geometry_data.h"
#include "engine/core/types/material_data.h"

#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/mesh.h"
#include "engine/renderer/model/skeleton.h"

#include "model_graph_node.h"

namespace ProcModel {

struct ModelGraph {
  std::string source_path;
  ModelGraphNode root;
  std::vector<Geometry::MeshData> mesh_data;
  std::vector<Content::MaterialData> materials;
  // The scene's nodes, which skinned meshes' bones name, and its animations
  Skeleton skeleton;
  std::vector<AnimationClip> animations;

  // Flat lookup for DescriptorResolver
  std::unordered_map<std::string, ModelGraphNode*> node_lookup;

  // GPU-uploaded meshes
  std::vector<std::unique_ptr<Mesh>> gpu_meshes;
  // The materials, registered once with the upload; meshes index them by
  // their material index
  std::vector<MaterialHandle> material_handles;

  bool UploadMeshes();
  const Mesh* GetMesh(int index) const;

  // ? Check if it can be declared like GetMesh()
  const std::vector<Content::MaterialData>& GetMaterials() const;
};

}  // namespace ProcModel

#endif  // MODEL_GRAPH_H

#ifndef MODEL_GRAPH_H
#define MODEL_GRAPH_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/types/geometry_data.h"
#include "engine/core/types/material_data.h"

#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/mesh.h"
#include "engine/animation/gait_set.h"
#include "engine/animation/pose_set.h"
#include "engine/animation/skeleton.h"

#include "model_graph_node.h"

namespace ProcModel {

struct ModelGraph {
  std::string source_path;
  ModelGraphNode root;
  std::vector<Geometry::MeshData> mesh_data;
  std::vector<Content::MaterialData> materials;
  // The scene's nodes, which skinned meshes' bones name, and its animations
  SkeletalData skeletal;
  // The descriptor's poses, when its clip is among the skeletal clips (built
  // additive)
  std::optional<PoseSet> poses;
  int pose_clip = -1;
  // The descriptor's gaits with their clips found (shared with the animators,
  // so edits reach every instance)
  std::shared_ptr<GaitSet> gaits;

  // Flat lookup for DescriptorResolver
  std::unordered_map<std::string, ModelGraphNode*> node_lookup;

  // GPU-uploaded meshes
  std::vector<std::unique_ptr<Mesh>> gpu_meshes;
  // The materials, registered once with the upload; meshes index them by
  // their material index
  std::vector<MaterialHandle> material_handles;

  // Uploads the meshes not uploaded yet; with a budget it stops once that
  // passes, to go on in a later call. False if the model has no meshes
  bool UploadMeshes(float budget_ms = 0.0f);
  bool Uploaded() const { return gpu_meshes.size() == mesh_data.size(); }
  const Mesh* GetMesh(int index) const;

  // ? Check if it can be declared like GetMesh()
  const std::vector<Content::MaterialData>& GetMaterials() const;
};

}  // namespace ProcModel

#endif  // MODEL_GRAPH_H

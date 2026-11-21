#include "model_graph.h"

namespace ProcModel {

bool ModelGraph::UploadMeshes() {
  gpu_meshes.reserve(mesh_data.size());
  for (const auto& md : mesh_data) {
    gpu_meshes.emplace_back(std::make_unique<Mesh>(md));
  }
  material_handles.clear();
  for (const auto& material : materials) {
    material_handles.push_back(MaterialRegistry::Instance().Register(material));
  }
  return !gpu_meshes.empty();
}

const Mesh* ModelGraph::GetMesh(int index) const {
  if (index < 0 || index >= static_cast<int>(gpu_meshes.size()))
    return nullptr;
  return gpu_meshes[index].get();
}

MaterialHandle ModelGraph::GetMaterialHandle(const Mesh& mesh) const {
  const uint32_t index = mesh.MaterialIndex();
  return index < material_handles.size() ? material_handles[index]
                                         : INVALID_MATERIAL;
}

const std::vector<Content::MaterialData>& ModelGraph::GetMaterials() const {
  return materials;
}

}  // namespace ProcModel

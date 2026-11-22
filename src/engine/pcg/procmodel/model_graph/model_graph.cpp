#include "model_graph.h"

#include <chrono>

namespace ProcModel {

bool ModelGraph::UploadMeshes(float budget_ms) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  if (gpu_meshes.empty()) {
    gpu_meshes.reserve(mesh_data.size());
    material_handles.clear();
    for (const auto& material : materials)
      material_handles.push_back(
          MaterialRegistry::Instance().Register(material));
  }
  while (gpu_meshes.size() < mesh_data.size()) {
    gpu_meshes.emplace_back(
        std::make_unique<Mesh>(mesh_data[gpu_meshes.size()]));
    if (budget_ms > 0.0f &&
        std::chrono::duration<float, std::milli>(Clock::now() - start)
                .count() >= budget_ms)
      break;
  }
  return !mesh_data.empty();
}

const Mesh* ModelGraph::GetMesh(int index) const {
  if (index < 0 || index >= static_cast<int>(gpu_meshes.size()))
    return nullptr;
  return gpu_meshes[index].get();
}

const std::vector<Content::MaterialData>& ModelGraph::GetMaterials() const {
  return materials;
}

}  // namespace ProcModel

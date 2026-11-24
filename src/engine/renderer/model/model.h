#ifndef MODEL_H
#define MODEL_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "engine/content/loaders/material_loader.h"
#include "engine/content/loaders/mesh_loader.h"

#include "engine/core/types/geometry_data.h"

#include "engine/memory/resource.h"

#include "engine/renderer/model/draw_settings.h"
#include "engine/renderer/model/mesh.h"

class Model : public Resource {
public:
  Model();
  ~Model() override;

  struct Metrics {
    uint32_t n_meshes = 0;
    uint32_t n_faces = 0;
    uint32_t n_vertices = 0;
    uint32_t n_materials = 0;

    glm::vec3 min_point = glm::vec3(FLT_MAX);
    glm::vec3 max_point = glm::vec3(-FLT_MAX);
    glm::vec3 origin = glm::vec3(0.0f);
    glm::vec3 centroid = glm::vec3(0.0f);
    float furthest = 0.0f;
  };

  const Metrics& GetMetrics() const { return metrics_; }

  // Resource pipeline for async loading
  ResourcePipe Create() {
    return std::move(
        Pipe() >> BIND_TASK(Model, LoadData) >>
        BIND_TASK_WITH_FLAGS(Model, UploadBuffers, TaskFlags::UseRenderThread));
  }

  // For synchronous loading, does not include metrics
  static std::shared_ptr<Model> Load(const std::string& file);

  // Set source file path
  void SetSource(const std::string& path) { source_path_ = path; }

  // TODO: remove the function
  void Draw(bgfx::ViewId view, bgfx::ProgramHandle program) const;

  uint32_t NLoadedMeshes() const;

  const Mesh* QueryMesh(uint32_t index) const;

  // Factory function
  static std::shared_ptr<Model> FromMeshData(
      const std::vector<Geometry::MeshData>& mesh_data,
      const std::vector<Content::MaterialData>& materials);

  // Computes metrics for a filtered subset of mesh_data
  Metrics ComputeFilteredMetrics(
      const std::vector<uint32_t>& mesh_filter) const;

  const std::vector<Content::MaterialData>& GetMaterials() const;

  // Applies its draw settings to its meshes; lods[i] is settings.lods[i]'s
  // model (null: that LOD is skipped)
  void SetDraw(const DrawSettings& settings,
               const std::vector<const Model*>& lods);

  // An animated model's skeleton and clips (its meshes are skinned to it);
  // null for the rest
  const SkeletalData* Skeletal() const { return skeletal_; }

  void Destroy() override;

private:
  static Metrics ComputeMetrics(
      const std::vector<Geometry::MeshData>& mesh_data);

  Metrics metrics_;

  std::string source_path_;

  //  Parse file, compute metrics
  bool LoadData();

  // Create BGFX resources
  bool UploadBuffers();

  // Intermediate CPU data
  std::vector<Geometry::MeshData> mesh_data_;
  std::vector<Content::MaterialData> materials_;

  // Final GPU mesh_data
  std::vector<std::unique_ptr<Mesh>> meshes_;

  const SkeletalData* skeletal_ = nullptr;  // owned by the mesh cache
};

#endif  // MODEL_H

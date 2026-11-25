#include "model.h"

#include "engine/content/cache/material_cache.h"
#include "engine/content/cache/mesh_cache.h"

#include "engine/core/logger.h"
#include "engine/renderer/material/material_registry.h"

Model::Model()
    : metrics_(), source_path_(), mesh_data_(), meshes_(), materials_() {}

Model::~Model() {
  meshes_.clear();
  mesh_data_.clear();
  materials_.clear();
}

std::shared_ptr<Model> Model::Load(const std::string& file) {
  // Get cached mesh_data
  const auto& mesh_list = Content::MeshCache::Instance().Get(file);

  // Get cached materials
  const auto& materials = Content::MaterialCache::Instance().Get(file);

  if (mesh_list.empty() && materials.empty()) {
    Logger::getInstance().Log(
        LogLevel::Error, "[Model::Load] No mesh_data or materials in: " + file);
    return nullptr;
  }

  auto out = std::make_shared<Model>();
  out->materials_ = materials;
  out->meshes_.reserve(mesh_list.size());

  for (const auto& geom : mesh_list)
    out->meshes_.emplace_back(std::make_unique<Mesh>(geom));

  Logger::getInstance().Log(
      LogLevel::Debug,
      "[Model] Loaded " + std::to_string(out->NLoadedMeshes()) +
          " mesh_data, " + std::to_string(out->materials_.size()) +
          " materials");

  return out;
}

bool Model::LoadData() {
  auto mesh_data = Content::MeshLoader::Load(source_path_, &materials_);

  if (mesh_data.empty()) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[Model::LoadData] failed - no mesh_data in " + source_path_);
    return false;
  }

  // Compute metrics while CPU data is available
  metrics_ = ComputeMetrics(mesh_data);

  // Store for upload phase
  mesh_data_ = std::move(mesh_data);
  textures_ = MaterialRegistry::Instance().PrepareTextures(materials_);

  Logger::getInstance().Log(
      LogLevel::Debug, "[Model] Data loaded: " + source_path_ +
                           ", mesh_data: " + std::to_string(mesh_data_.size()));
  return true;
}

std::shared_ptr<Model> Model::FromMeshData(
    const std::vector<Geometry::MeshData>& mesh_data,
    const std::vector<Content::MaterialData>& materials) {

  if (mesh_data.empty())
    return nullptr;

  auto out = std::make_shared<Model>();
  out->metrics_ = ComputeMetrics(mesh_data);
  out->mesh_data_ = mesh_data;  // needed for filtered metrics
  out->materials_ = materials;
  out->meshes_.reserve(mesh_data.size());

  for (const auto& geom : mesh_data)
    out->meshes_.emplace_back(std::make_unique<Mesh>(geom));

  return out;
}

uint32_t Model::NLoadedMeshes() const {
  return static_cast<uint32_t>(meshes_.size());
}

const Mesh* Model::QueryMesh(uint32_t index) const {
  return index < meshes_.size() ? meshes_[index].get() : nullptr;
}

// ! This function needs to be removed, SceneViewPipeline should handle this
// ! This is too tightly coupled to the renderer
void Model::Draw(bgfx::ViewId view, bgfx::ProgramHandle program) const {
  for (const auto& mesh : meshes_) {
    mesh->Bind();
    bgfx::setState(BGFX_STATE_DEFAULT);  // opaque
    bgfx::submit(view, program);
  }
}

bool Model::UploadBuffers() {
  if (mesh_data_.empty()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[Model::UploadBuffers] no data to upload");
    return false;
  }

  meshes_.reserve(mesh_data_.size());
  for (auto& md : mesh_data_)
    meshes_.emplace_back(std::make_unique<Mesh>(md));

  TextureRegistry::Instance().Upload(std::move(textures_));
  textures_.clear();

  // Free intermediate CPU data
  mesh_data_.clear();

  Logger::getInstance().Log(LogLevel::Debug,
                            "[Model] Buffers uploaded, mesh count: " +
                                std::to_string(NLoadedMeshes()));
  return true;
}

Model::Metrics Model::ComputeFilteredMetrics(
    const std::vector<uint32_t>& mesh_filter) const {
  std::vector<Geometry::MeshData> filtered;
  for (uint32_t idx : mesh_filter) {
    if (idx < mesh_data_.size())
      filtered.push_back(mesh_data_[idx]);
  }
  return ComputeMetrics(filtered);
}

Model::Metrics Model::ComputeMetrics(
    const std::vector<Geometry::MeshData>& mesh_data) {
  Metrics m{};
  m.n_meshes = static_cast<uint32_t>(mesh_data.size());

  glm::vec3 centroid_acc(0.0f);

  for (const auto& md : mesh_data) {
    m.n_vertices += static_cast<uint32_t>(md.vertices.size());
    m.n_faces += static_cast<uint32_t>(md.indices.size()) / 3;
    m.n_materials = std::max(m.n_materials, md.material_index + 1);

    for (const auto& v : md.vertices) {
      m.min_point = glm::min(m.min_point, v.position);
      m.max_point = glm::max(m.max_point, v.position);
      m.furthest = glm::max(m.furthest, glm::length(v.position));
      centroid_acc += v.position;
    }
  }

  if (m.n_vertices > 0) {
    m.centroid = centroid_acc / static_cast<float>(m.n_vertices);
  }

  m.origin = (m.min_point + m.max_point) * 0.5f;
  return m;
}

const std::vector<Content::MaterialData>& Model::GetMaterials() const {
  return materials_;
}

void Model::Destroy() {
  meshes_.clear();
  mesh_data_.clear();
  textures_.clear();
  metrics_ = Metrics{};
}

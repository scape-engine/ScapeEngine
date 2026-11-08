#include "model_entity_factory.h"

#include <cstring>
#include <memory>
#include <unordered_map>

#include "engine/core/logger.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/model.h"
#include "engine/renderer/primitives/shapes.h"

static constexpr const char* primitive_prefix = "primitive:";

// ponytail: models are never evicted, add refcounting when scenes get
// swapped often
static const Model* _LoadModel(const std::filesystem::path& path) {
  static std::unordered_map<std::string, std::shared_ptr<Model>> cache;
  const std::string key = path.lexically_normal().string();
  if (auto it = cache.find(key); it != cache.end())
    return it->second.get();

  // ! Synchronous loading
  std::shared_ptr<Model> model = Model::Load(key);
  if (!model)
    return nullptr;
  return cache.emplace(key, std::move(model)).first->second.get();
}

// Each shape's mesh is created once and shared by its entities
static const Mesh* _LoadPrimitive(const std::string& shape) {
  static std::unordered_map<std::string, const Mesh*> cache;
  if (auto it = cache.find(shape); it != cache.end())
    return it->second;

  const Mesh* mesh = nullptr;
  if (shape == "cube")          mesh = Shapes::CreateCube();
  else if (shape == "sphere")   mesh = Shapes::CreateSphere();
  else if (shape == "capsule")  mesh = Shapes::CreateCapsule();
  else if (shape == "cylinder") mesh = Shapes::CreateCylinder();
  else if (shape == "pyramid")  mesh = Shapes::CreatePyramid();
  else if (shape == "plane")    mesh = Shapes::CreatePlane();

  if (mesh)
    cache[shape] = mesh;
  return mesh;
}

static bool _Bind(MeshRendererComponent& renderer,
                  const std::filesystem::path& root) {
  if (renderer.source_.rfind(primitive_prefix, 0) == 0) {
    renderer.mesh_ =
        _LoadPrimitive(renderer.source_.substr(std::strlen(primitive_prefix)));
    return renderer.mesh_ != nullptr;
  }

  const Model* model = _LoadModel(root / renderer.source_);
  if (!model)
    return false;
  renderer.mesh_ = model->QueryMesh(renderer.mesh_index_);
  if (!renderer.mesh_)
    return false;

  const auto& materials = model->GetMaterials();
  const uint32_t material_index = renderer.mesh_->MaterialIndex();
  renderer.material_ =
      material_index < materials.size()
          ? MaterialRegistry::Instance().Register(materials[material_index])
          : INVALID_MATERIAL;
  return true;
}

Entity ModelEntityFactory::InstanceModel(ECS& ecs, const std::string& source,
                                         const std::filesystem::path& root) {
  const Model* model = _LoadModel(root / source);
  if (!model || model->NLoadedMeshes() == 0) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ModelEntityFactory] No meshes in " + source);
    return entt::null;
  }

  const Entity entity = std::get<0>(
      ecs.CreateEntity(std::filesystem::path(source).filename().string()));

  for (uint32_t i = 0; i < model->NLoadedMeshes(); ++i) {
    // Single mesh: keep it on the root so selecting the model selects (and
    // outlines) the mesh. Multiple meshes: one child entity each, since an
    // entity can only hold one MeshRenderer
    Entity mesh_entity = entity;
    if (model->NLoadedMeshes() > 1)
      mesh_entity = std::get<0>(
          ecs.CreateEntity("Mesh " + std::to_string(i), entity));

    auto& renderer = ecs.Add<MeshRendererComponent>(mesh_entity);
    renderer.source_ = source;
    renderer.mesh_index_ = i;
    _Bind(renderer, root);
  }
  return entity;
}

void ModelEntityFactory::AttachPrimitive(ECS& ecs, Entity entity,
                                         const std::string& shape) {
  auto& renderer = ecs.Add<MeshRendererComponent>(entity);
  renderer.source_ = primitive_prefix + shape;
  renderer.mesh_ = _LoadPrimitive(shape);
}

void ModelEntityFactory::BindMeshRenderers(ECS& ecs,
                                           const std::filesystem::path& root) {
  for (auto [entity, renderer] : ecs.View<MeshRendererComponent>().each()) {
    if (renderer.mesh_ || renderer.source_.empty())
      continue;
    // The source is kept, so saving again doesn't lose the reference
    if (!_Bind(renderer, root))
      Logger::getInstance().Log(
          LogLevel::Warning,
          "[ModelEntityFactory] Missing mesh source: " + renderer.source_);
  }
}

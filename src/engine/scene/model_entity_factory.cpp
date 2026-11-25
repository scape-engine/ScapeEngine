#include "model_entity_factory.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/memory/resource_manager.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/model.h"
#include "engine/renderer/primitives/shapes.h"

static constexpr const char* primitive_prefix = "primitive:";

// Models by absolute path, loaded or loading. nullptr once a load failed
// ponytail: models are never evicted, add refcounting when scenes get
// swapped often
static std::unordered_map<std::string, std::shared_ptr<Model>> models;

static std::string _Key(const std::filesystem::path& root,
                        const std::string& source) {
  return (root / source).lexically_normal().string();
}

static bool _IsLoading(const Model& model) {
  const ResourceState state = model.GetState();
  return state == ResourceState::QUEUED || state == ResourceState::LOADING;
}

// The loaded model of a path, or nullptr while it loads (loading = true) or
// if it failed. Models not requested yet start loading in the background:
// parsing and decoding on the resource worker, GPU upload on the render
// thread (ResourceManager::UpdateContext, every frame)
static const Model* _Get(const std::string& key, bool& loading) {
  loading = false;
  auto it = models.find(key);
  if (it == models.end()) {
    ResourceManager& resources = EngineContext::resourceManager();
    // Named after the file: the status bar shows "Loading <name>"
    auto [id, model] = resources.Create<Model>(
        std::filesystem::path(key).filename().string());
    model->SetSource(key);
    models[key] = model;
    resources.Execute(model->Create());  // sets FAILED if it can't queue
    loading = true;
    return nullptr;
  }

  if (!it->second)
    return nullptr;  // failed, already reported
  if (_IsLoading(*it->second)) {
    loading = true;
    return nullptr;
  }
  if (it->second->GetState() == ResourceState::FAILED) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "[ModelEntityFactory] Failed to load " + key);
    it->second = nullptr;
    return nullptr;
  }
  return it->second.get();
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

static bool _IsPrimitive(const std::string& source) {
  return source.rfind(primitive_prefix, 0) == 0;
}

static bool _BindPrimitive(MeshRendererComponent& renderer) {
  renderer.mesh_ =
      _LoadPrimitive(renderer.source_.substr(std::strlen(primitive_prefix)));
  return renderer.mesh_ != nullptr;
}

static bool _BindModel(MeshRendererComponent& renderer, const Model& model) {
  renderer.mesh_ = model.QueryMesh(renderer.mesh_index_);
  if (!renderer.mesh_)
    return false;

  const auto& materials = model.GetMaterials();
  const uint32_t material_index = renderer.mesh_->MaterialIndex();
  renderer.material_ =
      material_index < materials.size()
          ? MaterialRegistry::Instance().Register(materials[material_index])
          : INVALID_MATERIAL;
  return true;
}

static void _AddMeshRenderers(ECS& ecs, Entity entity, const Model& model,
                              const std::string& source) {
  if (model.NLoadedMeshes() == 0)
    Logger::getInstance().Log(LogLevel::Error,
                              "[ModelEntityFactory] No meshes in " + source);

  for (uint32_t i = 0; i < model.NLoadedMeshes(); ++i) {
    // Single mesh: keep it on the root so selecting the model selects (and
    // outlines) the mesh. Multiple meshes: one child entity each, since an
    // entity can only hold one MeshRenderer
    Entity mesh_entity = entity;
    if (model.NLoadedMeshes() > 1)
      mesh_entity = std::get<0>(
          ecs.CreateEntity("Mesh " + std::to_string(i), entity));

    auto& renderer = ecs.Add<MeshRendererComponent>(mesh_entity);
    renderer.source_ = source;
    renderer.mesh_index_ = i;
    _BindModel(renderer, model);
  }
}

Entity ModelEntityFactory::InstanceModel(ECS& ecs, const std::string& source,
                                         const std::filesystem::path& root) {
  const std::string key = _Key(root, source);

  // Retry a model that failed before: the file may have been fixed since
  if (auto it = models.find(key); it != models.end() && !it->second)
    models.erase(it);

  const Entity entity = std::get<0>(
      ecs.CreateEntity(std::filesystem::path(source).filename().string()));

  bool loading = false;
  if (const Model* model = _Get(key, loading))
    _AddMeshRenderers(ecs, entity, *model, source);  // already loaded
  else
    ecs.Add<PendingModelComponent>(entity, PendingModelComponent{source});
  return entity;
}

void ModelEntityFactory::AttachPrimitive(ECS& ecs, Entity entity,
                                         const std::string& shape) {
  auto& renderer = ecs.Add<MeshRendererComponent>(entity);
  renderer.source_ = primitive_prefix + shape;
  _BindPrimitive(renderer);
}

size_t ModelEntityFactory::BindPending(ECS& ecs,
                                       const std::filesystem::path& root) {
  size_t waiting = 0;

  // Pending models. Collected first: expanding adds entities and components
  std::vector<Entity> pending;
  for (auto [entity, model] : ecs.View<PendingModelComponent>().each())
    pending.push_back(entity);

  for (Entity entity : pending) {
    const std::string source = ecs.Get<PendingModelComponent>(entity).source_;
    bool loading = false;
    const Model* model = _Get(_Key(root, source), loading);
    if (loading)
      ++waiting;
    if (!model)
      continue;  // a failed one stays pending, so saving keeps its source
    ecs.Remove<PendingModelComponent>(entity);
    _AddMeshRenderers(ecs, entity, *model, source);
  }

  // Mesh renderers loaded from a scene
  for (auto [entity, renderer] : ecs.View<MeshRendererComponent>().each()) {
    if (renderer.mesh_ || renderer.source_.empty())
      continue;

    // The source is kept, so saving again doesn't lose the reference
    if (_IsPrimitive(renderer.source_)) {
      if (!_BindPrimitive(renderer))
        Logger::getInstance().Log(
            LogLevel::Warning,
            "[ModelEntityFactory] Unknown primitive: " + renderer.source_);
      continue;
    }

    bool loading = false;
    const Model* model = _Get(_Key(root, renderer.source_), loading);
    if (loading)
      ++waiting;
    if (!model || _BindModel(renderer, *model))
      continue;

    // Called every frame while loading: report each bad index once
    static std::unordered_set<std::string> reported;
    const std::string mesh =
        renderer.source_ + " #" + std::to_string(renderer.mesh_index_);
    if (reported.insert(mesh).second)
      Logger::getInstance().Log(LogLevel::Warning,
                                "[ModelEntityFactory] No mesh " + mesh);
  }
  return waiting;
}

bool ModelEntityFactory::IsLoading() {
  return std::any_of(models.begin(), models.end(), [](const auto& entry) {
    return entry.second && _IsLoading(*entry.second);
  });
}

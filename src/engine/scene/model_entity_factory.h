// ---------------------------------------------------------------------------
// model_entity_factory.h
// Creates entities from models and binds mesh renderers to their meshes.
// Sources are project-relative model paths, or "primitive:<shape>".
// ---------------------------------------------------------------------------

#ifndef MODEL_ENTITY_FACTORY_H
#define MODEL_ENTITY_FACTORY_H

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "engine/ecs/world.h"
#include "engine/renderer/material/material_registry.h"

class Mesh;

class ModelEntityFactory {
public:
  // One entity for the model, with one child per mesh when it has several.
  // Returns entt::null if the model can't be loaded
  static Entity InstanceModel(ECS& ecs, const std::string& source,
                              const std::filesystem::path& root);

  // Adds a mesh renderer with a primitive shape ("cube", "sphere", ...)
  static void AttachPrimitive(ECS& ecs, Entity entity,
                              const std::string& shape);

  // A source's meshes with their materials, loading the model on first use;
  // none if it can't be loaded
  static std::vector<std::pair<const Mesh*, MaterialHandle>> Meshes(
      const std::string& source, const std::filesystem::path& root);

  // The cache key of a model source (its normalised path under the root):
  // MeshCache and MaterialCache entries read under it serve its first load
  static std::string ModelKey(const std::string& source,
                              const std::filesystem::path& root);

  // Applies a loaded model's draw settings again (after they were edited);
  // nothing if it isn't loaded
  static void ReloadDrawSettings(const std::string& source,
                                 const std::filesystem::path& root);

  // Resolves the mesh and material of every mesh renderer that has a source
  // but no mesh
  static void BindMeshRenderers(ECS& ecs, const std::filesystem::path& root);
};

#endif  // MODEL_ENTITY_FACTORY_H

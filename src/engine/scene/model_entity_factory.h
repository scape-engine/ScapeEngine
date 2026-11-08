// ---------------------------------------------------------------------------
// model_entity_factory.h
// Creates entities from models and binds mesh renderers to their meshes.
// Sources are project-relative model paths, or "primitive:<shape>".
// ---------------------------------------------------------------------------

#ifndef MODEL_ENTITY_FACTORY_H
#define MODEL_ENTITY_FACTORY_H

#include <filesystem>
#include <string>

#include "engine/ecs/world.h"

class ModelEntityFactory {
public:
  // One entity for the model, with one child per mesh when it has several.
  // Returns entt::null if the model can't be loaded
  static Entity InstanceModel(ECS& ecs, const std::string& source,
                              const std::filesystem::path& root);

  // Adds a mesh renderer with a primitive shape ("cube", "sphere", ...)
  static void AttachPrimitive(ECS& ecs, Entity entity,
                              const std::string& shape);

  // Resolves the mesh and material of every mesh renderer that has a source
  // but no mesh
  static void BindMeshRenderers(ECS& ecs, const std::filesystem::path& root);
};

#endif  // MODEL_ENTITY_FACTORY_H

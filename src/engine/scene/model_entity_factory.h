// ---------------------------------------------------------------------------
// model_entity_factory.h
// Creates entities from models and binds mesh renderers to their meshes.
// Sources are project-relative model paths, or "primitive:<shape>".
// ---------------------------------------------------------------------------

#ifndef MODEL_ENTITY_FACTORY_H
#define MODEL_ENTITY_FACTORY_H

#include <cstddef>
#include <filesystem>
#include <string>

#include "engine/ecs/world.h"

class ModelEntityFactory {
public:
  // One entity for the model, with one child per mesh when it has several.
  // A model that isn't loaded yet loads in the background: the entity is
  // returned right away with a PendingModelComponent, and BindPending gives
  // it its meshes once loaded
  static Entity InstanceModel(ECS& ecs, const std::string& source,
                              const std::filesystem::path& root);

  // Adds a mesh renderer with a primitive shape ("cube", "sphere", ...)
  static void AttachPrimitive(ECS& ecs, Entity entity,
                              const std::string& shape);

  // Gives pending models their mesh renderers, and resolves the mesh and
  // material of mesh renderers that have a source but no mesh. Models that
  // aren't loaded start loading in the background: call again (e.g. every
  // frame) while it returns entities still waiting, or IsLoading()
  static size_t BindPending(ECS& ecs, const std::filesystem::path& root);

  // True while any model loads in the background
  static bool IsLoading();
};

#endif  // MODEL_ENTITY_FACTORY_H

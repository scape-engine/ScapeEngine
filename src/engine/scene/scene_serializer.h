// ---------------------------------------------------------------------------
// scene_serializer.h
// Converts the entities of an ECS to and from scene JSON.
// Paths inside components are stored relative to the project root.
// ---------------------------------------------------------------------------

#ifndef SCENE_SERIALIZER_H
#define SCENE_SERIALIZER_H

#include <filesystem>
#include <nlohmann/json.hpp>

class ECS;

class SceneSerializer {
public:
  static nlohmann::json Serialize(ECS& ecs, const std::filesystem::path& root);

  // Replaces the content of the ECS. Mesh renderers get their source back but
  // no mesh: bind them with ModelEntityFactory::BindMeshRenderers
  static bool Deserialize(ECS& ecs, const nlohmann::json& scene,
                          const std::filesystem::path& root);
};

#endif  // SCENE_SERIALIZER_H

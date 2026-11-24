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
struct TransformComponent;

class SceneSerializer {
public:
  static nlohmann::json Serialize(ECS& ecs, const std::filesystem::path& root);

  // Replaces the content of the ECS. Mesh renderers get their source back but
  // no mesh: bind them with ModelEntityFactory::BindMeshRenderers
  static bool Deserialize(ECS& ecs, const nlohmann::json& scene,
                          const std::filesystem::path& root);

  // Editor camera stored next to the entities: pose and move speed
  static void WriteEditorCamera(nlohmann::json& scene,
                                const TransformComponent& camera,
                                float move_speed);

  // False and nothing changed when the scene has no editor camera
  static bool ReadEditorCamera(const nlohmann::json& scene,
                               TransformComponent& camera, float& move_speed);
};

#endif  // SCENE_SERIALIZER_H

// Checks must run in release builds too
#undef NDEBUG
#include <cassert>

#include <iostream>

#include "engine/ecs/world.h"
#include "engine/scene/scene_serializer.h"

namespace Tests {

// A scene survives a save/load round trip: names, hierarchy, transforms,
// components and mesh sources
void TestSceneSerializer() {
  ECS ecs;
  {
    auto [first, first_transform] = ecs.CreateEntity("First");
    first_transform.position_ = glm::vec3(1.0f, 2.0f, 3.0f);
    auto [child, child_transform] = ecs.CreateEntity("Child", first);
    ecs.Add<PointLightComponent>(child).range_ = 42.0f;
    auto& renderer = ecs.Add<MeshRendererComponent>(child);
    renderer.source_ = "models/tower.glb";
    renderer.mesh_index_ = 2;
    ecs.CreateEntity("Second");
  }

  const nlohmann::json scene = SceneSerializer::Serialize(ecs, "/project");

  // Roots are written oldest first
  assert(scene.at("entities").at(0).at("name") == "First");

  ECS loaded;
  assert(SceneSerializer::Deserialize(loaded, scene, "/project"));
  assert(SceneSerializer::Serialize(loaded, "/project") == scene);

  int count = 0;
  for (auto [entity, transform] : loaded.View<TransformComponent>().each()) {
    ++count;
    if (transform.name_ == "First") {
      assert(transform.position_ == glm::vec3(1.0f, 2.0f, 3.0f));
      assert(transform.children_.size() == 1);
    }
    if (transform.name_ == "Child") {
      assert(loaded.Get<TransformComponent>(transform.parent_).name_ ==
             "First");
      assert(loaded.Get<PointLightComponent>(entity).range_ == 42.0f);
      const auto& renderer = loaded.Get<MeshRendererComponent>(entity);
      assert(renderer.source_ == "models/tower.glb");
      assert(renderer.mesh_index_ == 2 && renderer.mesh_ == nullptr);
    }
  }
  assert(count == 3);

  // The editor camera is optional: old scenes leave it untouched
  TransformComponent camera;
  camera.position_ = glm::vec3(4.0f, 5.0f, 6.0f);
  float speed = 16.0f;
  assert(!SceneSerializer::ReadEditorCamera(scene, camera, speed));
  assert(camera.position_ == glm::vec3(4.0f, 5.0f, 6.0f) && speed == 16.0f);

  nlohmann::json with_camera = scene;
  camera.rotation_ = glm::quat(0.5f, 0.5f, 0.5f, 0.5f);
  camera.euler_angles_ = glm::vec3(10.0f, 20.0f, 0.0f);
  SceneSerializer::WriteEditorCamera(with_camera, camera, 42.0f);
  assert(SceneSerializer::Deserialize(loaded, with_camera, "/project"));
  TransformComponent restored;
  assert(SceneSerializer::ReadEditorCamera(with_camera, restored, speed));
  assert(restored.position_ == camera.position_);
  assert(restored.rotation_ == camera.rotation_);
  assert(restored.euler_angles_ == camera.euler_angles_);
  assert(speed == 42.0f);

  // Unknown versions are rejected without touching the ECS
  assert(!SceneSerializer::Deserialize(loaded, {{"version", 99}}, "/project"));
  assert(loaded.View<TransformComponent>().size() == 3);

  std::cout << "Scene serializer tests passed" << std::endl;
}

}  // namespace Tests

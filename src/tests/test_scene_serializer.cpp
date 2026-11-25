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
    auto [second, second_transform] = ecs.CreateEntity("Second");
    ecs.Add<PendingModelComponent>(second,
                                   PendingModelComponent{"models/sponza.gltf"});
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

  // A model still loading when saved loads as pending again
  auto pending = loaded.View<PendingModelComponent>();
  assert(pending.size() == 1);
  assert(loaded.Get<PendingModelComponent>(pending.front()).source_ ==
         "models/sponza.gltf");

  // Unknown versions are rejected without touching the ECS
  assert(!SceneSerializer::Deserialize(loaded, {{"version", 99}}, "/project"));
  assert(loaded.View<TransformComponent>().size() == 3);

  std::cout << "Scene serializer tests passed" << std::endl;
}

}  // namespace Tests

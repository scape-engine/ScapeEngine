#ifndef WORLD_H
#define WORLD_H

#include <entt/entt.hpp>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include <algorithm> 

#include "engine/ecs/components/camera_component.h"
#include "engine/ecs/components/mesh_renderer_component.h"
#include "engine/ecs/components/transform_component.h"
#include "engine/ecs/components/light_component.h"

using Entity = entt::entity;
using Registry = entt::registry;
using Camera = std::tuple<TransformComponent&, CameraComponent&>;

class ECS {
public:
  ECS();

  static ECS& Main();

  /* ---------- entity creation ---------- */
  std::tuple<Entity, TransformComponent&> CreateEntity(
      const std::string& name = "Entity");
  std::tuple<Entity, TransformComponent&> CreateEntity(const std::string& name,
                                                       Entity parent);

  /* ---------- hierarchy ---------- */
  void SetParent(Entity child, Entity parent);
  void RemoveParent(Entity child);

  /* ---------- queries ---------- */
  std::optional<Camera> GetActiveCamera();
  // Iterable of (Entity, TransformComponent&, MeshRendererComponent&).
  // Bind by value: for (auto [e, transform, renderer] : GetRenderQueue())
  // TODO: sort by shader/material once those systems exist
  auto GetRenderQueue() {
    return registry_.view<TransformComponent, MeshRendererComponent>().each();
  }

  /* ---------- per-frame ---------- */
  void UpdateTransforms();

  /* ---------- registry access ---------- */
  Registry& Reg() { return registry_; }

  /* ---------- templates ---------- */
  template <typename... Components>
  auto View() {
    return registry_.view<Components...>();
  }

  template <typename T>
  bool Has(Entity entity) const {
    return registry_.any_of<T>(entity);
  }

  template <typename T>
  bool CanAdd(Entity entity) const {
    return !registry_.any_of<T>(entity);
  }

  template <typename T, typename... Args>
  T& Add(Entity entity, Args&&... args) {
    return registry_.emplace<T>(entity, std::forward<Args>(args)...);
  }

  template <typename T>
  T& Get(Entity entity) {
    return registry_.get<T>(entity);
  }

  template <typename T>
  void Remove(Entity entity) {
    registry_.remove<T>(entity);
  }

private:
  Registry registry_;
  uint32_t id_counter_ = 0;

  uint32_t GetId();
};

#endif  // WORLD_H
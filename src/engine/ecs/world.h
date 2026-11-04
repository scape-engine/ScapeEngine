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

  // Destroys the entity and its whole subtree
  void DestroyEntity(Entity entity);
  // Copies every component of the entity and its subtree; the copy gets the
  // same parent. Returns the copy, or entt::null for an invalid entity
  Entity DuplicateEntity(Entity entity);

  /* ---------- hierarchy ---------- */
  void SetParent(Entity child, Entity parent);
  void RemoveParent(Entity child);
  // Moves the entity among its parent's children, right before (or after)
  // the given sibling. Both must share a parent
  void MoveNextTo(Entity entity, Entity sibling, bool after);
  // True if ancestor is the entity itself or one of its parents
  bool IsAncestorOf(Entity ancestor, Entity entity);

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
#include "world.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "engine/core/logger.h"
#include "engine/renderer/model/model.h"

/* ---------- helpers ---------- */
// Sets an entire subtree dirty and fixes depth values bottom-up.
static void _SetSubtreeDirtyAndFixDepth(ECS& ecs, Entity root, uint32_t depth) {
  if (!ecs.Has<TransformComponent>(root))
    return;

  auto& t = ecs.Get<TransformComponent>(root);
  t.depth_ = depth;
  t.modified_ = true;

  for (Entity c : t.children_) {
    if (ecs.Has<TransformComponent>(c)) {
      _SetSubtreeDirtyAndFixDepth(ecs, c, depth + 1);
    }
  }
}

/* ───────── singleton ───────── */
ECS& ECS::Main() {
  if (!entt::locator<ECS>::has_value())
    Logger::getInstance().Log(LogLevel::Error,
                              "Fatal: No main ECS service was instantiated");

  return entt::locator<ECS>::value();
}

/* ───────── constructor ───────── */
// The render queue used to be a vector of component references rebuilt on
// every MeshRenderer add/remove: O(n^2) on load, and references dangled when
// entt swap-and-popped storage. GetRenderQueue() is now a live view.
ECS::ECS() : registry_(), id_counter_(0) {}

uint32_t ECS::GetId() {
  return ++id_counter_;
}

/* ---------- entity creation ---------- */
std::tuple<Entity, TransformComponent&> ECS::CreateEntity(
    const std::string& name) {
  Entity entity = registry_.create();
  TransformComponent& transform = Add<TransformComponent>(entity);

  transform.id_ = GetId();
  transform.name_ = name;
  transform.modified_ = true;

  return std::tuple<Entity, TransformComponent&>(entity, transform);
}

std::tuple<Entity, TransformComponent&> ECS::CreateEntity(
    const std::string& name, Entity parent) {
  if (!Has<TransformComponent>(parent)) {
    Logger::getInstance().Log(
        LogLevel::Warning,
        "[ECS]: Tried to create entity '" + name + "' with invalid parent");
    return CreateEntity(name);
  }

  auto [entity, transform] = CreateEntity(name);
  SetParent(entity, parent);
  return std::tuple<Entity, TransformComponent&>(entity, transform);
}

void ECS::DestroyEntity(Entity entity) {
  if (!Has<TransformComponent>(entity))
    return;

  // Copy: destroying a child removes it from this list
  const std::vector<Entity> children =
      Get<TransformComponent>(entity).children_;
  for (Entity child : children)
    DestroyEntity(child);

  RemoveParent(entity);
  registry_.destroy(entity);
}

Entity ECS::DuplicateEntity(Entity entity) {
  if (!Has<TransformComponent>(entity))
    return entt::null;

  // Copy every component the entity has. Storages of types that can't be
  // copied leave the component out
  const Entity copy = registry_.create();
  for (auto [id, storage] : registry_.storage()) {
    if (storage.contains(entity) && !storage.contains(copy))
      storage.push(copy, storage.value(entity));
  }

  // The copied transform still points at the original's family: give it its
  // own id and no children, then attach it next to the original
  TransformComponent& transform = Get<TransformComponent>(copy);
  const Entity parent = transform.parent_;
  const std::vector<Entity> children = transform.children_;
  transform.id_ = GetId();
  transform.children_.clear();
  transform.parent_ = entt::null;
  transform.depth_ = 0;
  transform.modified_ = true;
  if (parent != entt::null)
    SetParent(copy, parent);

  for (Entity child : children) {
    const Entity child_copy = DuplicateEntity(child);
    if (child_copy != entt::null)
      SetParent(child_copy, copy);
  }
  return copy;
}

/* ---------- hierarchy ---------- */
void ECS::MoveNextTo(Entity entity, Entity sibling, bool after) {
  if (entity == sibling || !Has<TransformComponent>(entity) ||
      !Has<TransformComponent>(sibling))
    return;

  const Entity parent = Get<TransformComponent>(sibling).parent_;
  if (parent == entt::null || Get<TransformComponent>(entity).parent_ != parent)
    return;

  auto& children = Get<TransformComponent>(parent).children_;
  children.erase(std::remove(children.begin(), children.end(), entity),
                 children.end());
  auto it = std::find(children.begin(), children.end(), sibling);
  if (after && it != children.end())
    ++it;
  children.insert(it, entity);
}

bool ECS::IsAncestorOf(Entity ancestor, Entity entity) {
  while (entity != entt::null && Has<TransformComponent>(entity)) {
    if (entity == ancestor)
      return true;
    entity = Get<TransformComponent>(entity).parent_;
  }
  return false;
}

void ECS::SetParent(Entity entity, Entity parent) {
  if (!Has<TransformComponent>(entity)) {
    Logger::getInstance().Log(
        LogLevel::Warning, "[ECS]: Tried to modify parent of invalid entity");
    return;
  }

  if (!Has<TransformComponent>(parent)) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "[ECS]: Tried to make invalid entity a parent");
    return;
  }

  RemoveParent(entity);

  TransformComponent& transform = Get<TransformComponent>(entity);
  TransformComponent& parent_transform = Get<TransformComponent>(parent);

  parent_transform.children_.push_back(entity);
  transform.parent_ = parent;
  _SetSubtreeDirtyAndFixDepth(*this, entity, parent_transform.depth_ + 1);
}

void ECS::RemoveParent(Entity entity) {
  TransformComponent& transform = Get<TransformComponent>(entity);

  // TODO: replace with Transform implementation
  // Directly check parent_ member
  if (transform.parent_ == entt::null)
    return;

  // Get parent's children_ vector
  TransformComponent& parent = Get<TransformComponent>(transform.parent_);
  auto& children = parent.children_;
  // Erase, not swap-and-pop: the hierarchy shows children in this order
  children.erase(std::remove(children.begin(), children.end(), entity),
                 children.end());

  // Example:
  /* if (!Transform::HasParent(transform))
    return;

  auto& children = Transform::GetParent(transform).children_;
  auto it = std::find(children.begin(), children.end(), entity);

  if (it != children.end()) {
    std::swap(*it, children.back());
    children.pop_back();
  }*/

  transform.parent_ = entt::null;
  _SetSubtreeDirtyAndFixDepth(*this, entity, 0);
}

/* ---------- camera query ---------- */
std::optional<Camera> ECS::GetActiveCamera() {
  auto group = registry_.group<TransformComponent>(entt::get<CameraComponent>);
  for (auto entity : group) {
    auto [transform, camera] =
        group.get<TransformComponent, CameraComponent>(entity);
    if (camera.enabled_)
      return Camera(transform, camera);
  }
  return std::nullopt;
}

/* ---------- per-frame transform evaluation ---------- */
// TODO: check if we need to directly use TransformSystem here
/* void ECS::UpdateTransforms() {
  registry_.view<TransformComponent>().each([](auto& t) {
    if (!t.modified_)
      return;
    t.model_ = glm::translate(glm::mat4(1.0f), t.position_) *
               glm::mat4_cast(t.rotation_) *
               glm::scale(glm::mat4(1.0f), t.scale_);
    t.normal_ = glm::transpose(glm::inverse(glm::mat3(t.model_)));
    t.modified_ = false;
  });
} */

void ECS::Clear() {
  registry_.clear();
  id_counter_ = 0;
}

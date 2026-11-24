#include "fade_system.h"

#include <algorithm>

#include "engine/ecs/components/fade_component.h"

namespace FadeSystem {

void Update(ECS& ecs, float delta) {
  std::vector<Entity> hidden;
  for (auto [entity, fade] : ecs.View<FadeComponent>().each()) {
    const float step =
        fade.duration_ > 0.0f ? delta / fade.duration_ : 1.0f;
    if (fade.visibility_ < fade.target_)
      fade.visibility_ = std::min(fade.visibility_ + step, fade.target_);
    else
      fade.visibility_ = std::max(fade.visibility_ - step, fade.target_);
    fade.visibility_ = std::clamp(fade.visibility_, 0.0f, 1.0f);
    if (fade.destroy_when_hidden_ && fade.visibility_ <= 0.0f &&
        fade.target_ <= 0.0f)
      hidden.push_back(entity);
  }
  // A destroyed parent takes its children with it: skip those
  for (const Entity entity : hidden)
    if (ecs.Reg().valid(entity))
      ecs.DestroyEntity(entity);
}

float Visibility(ECS& ecs, Entity entity) {
  // Most frames no entity fades: no walk up the hierarchy then
  if (ecs.Reg().storage<FadeComponent>().empty())
    return 1.0f;
  float visibility = 1.0f;
  while (entity != entt::null && ecs.Reg().valid(entity)) {
    if (const auto* fade = ecs.Reg().try_get<FadeComponent>(entity))
      visibility *= fade->visibility_;
    const auto* transform = ecs.Reg().try_get<TransformComponent>(entity);
    entity = transform ? transform->parent_ : entt::null;
  }
  return visibility;
}

}  // namespace FadeSystem

#include "animation.h"

#include "engine/ecs/components/animator_component.h"

namespace Animation {

static const AnimationClip* _Clip(const AnimatorComponent& animator) {
  const auto& clips = animator.data_->clips;
  return animator.clip_ >= 0 && animator.clip_ < static_cast<int>(clips.size())
             ? &clips[animator.clip_]
             : nullptr;
}

AnimatorComponent* AddAnimator(ECS& ecs, Entity entity,
                               const SkeletalData* data) {
  if (!data || data->Empty())
    return nullptr;
  auto& animator = ecs.Add<AnimatorComponent>(entity);
  animator.data_ = data;
  SamplePose(data->skeleton, _Clip(animator), 0.0f, animator.loop_,
             animator.pose_);
  return &animator;
}

void Update(ECS& ecs, float dt) {
  ecs.View<AnimatorComponent>().each([&](AnimatorComponent& animator) {
    if (!animator.data_ || !animator.playing_)
      return;
    animator.time_ += dt * animator.speed_;
    SamplePose(animator.data_->skeleton, _Clip(animator), animator.time_,
               animator.loop_, animator.pose_);
  });
}

const glm::mat4* Pose(ECS& ecs, Entity animator, uint32_t& count) {
  count = 0;
  if (animator == entt::null || !ecs.Reg().valid(animator))
    return nullptr;
  const auto* component = ecs.Reg().try_get<AnimatorComponent>(animator);
  if (!component || component->pose_.empty())
    return nullptr;
  count = static_cast<uint32_t>(component->pose_.size());
  return component->pose_.data();
}

}  // namespace Animation

#include "animation.h"

#include "engine/ecs/components/animator_component.h"

namespace Animation {

static const AnimationClip* _Clip(const AnimatorComponent& animator) {
  const auto& clips = animator.data_->clips;
  return animator.clip_ >= 0 && animator.clip_ < static_cast<int>(clips.size())
             ? &clips[animator.clip_]
             : nullptr;
}

static float _Ratio(const AnimatorComponent& animator, int clip, float time) {
  const auto& clips = animator.data_->clips;
  return clip >= 0 && clip < static_cast<int>(clips.size())
             ? ClipRatio(clips[clip], time, animator.loop_)
             : 0.0f;
}

// The current clip over the one fading out, by how far the fade has run
static void _Sample(AnimatorComponent& animator) {
  std::vector<PoseSampler::Layer> layers{
      {animator.clip_, _Ratio(animator, animator.clip_, animator.time_), 1.0f}};
  if (animator.fade_clip_ >= 0) {
    const float in = animator.fade_elapsed_ / animator.fade_length_;
    layers[0].weight = in;
    layers.push_back({animator.fade_clip_,
                      _Ratio(animator, animator.fade_clip_,
                             animator.fade_clip_time_),
                      1.0f - in});
  }
  animator.sampler_->Sample(*animator.data_, layers, animator.pose_);
}

AnimatorComponent* AddAnimator(ECS& ecs, Entity entity,
                               const SkeletalData* data) {
  if (!data || data->Empty())
    return nullptr;
  auto& animator = ecs.Add<AnimatorComponent>(entity);
  animator.data_ = data;
  animator.sampler_ = std::make_shared<PoseSampler>();
  _Sample(animator);
  return &animator;
}

void Play(AnimatorComponent& animator, int clip, float fade) {
  if (clip == animator.clip_)
    return;
  animator.fade_clip_ = fade > 0.0f && _Clip(animator) ? animator.clip_ : -1;
  animator.fade_clip_time_ = animator.time_;
  animator.fade_length_ = fade;
  animator.fade_elapsed_ = 0.0f;
  animator.clip_ = clip;
  animator.time_ = 0.0f;
}

void Update(ECS& ecs, float dt) {
  ecs.View<AnimatorComponent>().each([&](AnimatorComponent& animator) {
    if (!animator.data_ || !animator.playing_)
      return;
    animator.time_ += dt * animator.speed_;
    if (animator.fade_clip_ >= 0) {
      animator.fade_clip_time_ += dt * animator.speed_;
      animator.fade_elapsed_ += dt;
      if (animator.fade_elapsed_ >= animator.fade_length_)
        animator.fade_clip_ = -1;
    }
    _Sample(animator);
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

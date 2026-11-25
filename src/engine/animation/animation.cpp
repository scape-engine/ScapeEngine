#include "animation.h"

#include "engine/paging/viewers.h"

#include <algorithm>

#include "engine/diagnostics/frame_stats.h"
#include "engine/ecs/components/animator_component.h"
#include "engine/ecs/components/ground_component.h"
#include "engine/ecs/components/transform_component.h"

namespace Animation {

static const AnimationClip* _Clip(const AnimatorComponent& animator) {
  const auto& clips = animator.data_->clips;
  return animator.clip_ >= 0 && animator.clip_ < static_cast<int>(clips.size())
             ? &clips[animator.clip_]
             : nullptr;
}

static uint32_t _frame = 0;

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
  layers.insert(layers.end(), animator.poses_.begin(), animator.poses_.end());
  animator.sampler_->Sample(*animator.data_, layers, animator.pose_);
}

// Feet on the ground under them: each foot rises (or falls) by the ground's
// height over the model's own ground plane under it, and the body drops by
// the deepest fall so every leg reaches
static void _Plant(ECS& ecs, Entity entity, AnimatorComponent& animator) {
  const SkeletalData& data = *animator.data_;
  const AnimatorSettings& settings = animator.settings_;
  if (!settings.plant_feet || data.legs.empty() ||
      animator.ground_ == entt::null)
    return;
  auto& registry = ecs.Reg();
  const auto* ground = registry.try_get<GroundComponent>(animator.ground_);
  const auto* own = registry.try_get<TransformComponent>(entity);
  const auto* base = registry.try_get<TransformComponent>(animator.ground_);
  if (!ground || !ground->mesh_ || !own || !base ||
      Viewers::Distance(glm::vec3(own->model_[3])) > settings.plant_distance)
    return;
  // Model space to the ground's, and back
  const glm::mat4 to_ground = glm::inverse(base->model_) * own->model_;
  const glm::mat4 to_model = glm::inverse(to_ground);

  // Heights against the ground under the origin, which the model stands on
  // (the ground's mesh may be coarser than the surface it was placed on)
  glm::vec3 under;
  const glm::vec3 origin(to_ground[3]);
  if (!ground->mesh_->Under(origin, under))
    return;
  const float standing = (to_model * glm::vec4(under - origin, 0.0f)).y;

  std::vector<float> rises(data.legs.size(), 0.0f);
  float drop = 0.0f;
  bool any = false;
  for (size_t i = 0; i < data.legs.size(); ++i) {
    const Leg& leg = data.legs[i];
    const glm::vec3 foot(animator.pose_[leg.end][3]);
    const glm::vec3 flat =
        glm::vec3(to_ground * glm::vec4(foot.x, 0.0f, foot.z, 1.0f));
    glm::vec3 hit;
    if (!ground->mesh_->Under(flat, hit))
      continue;
    const float reach =
        settings.plant_reach *
        glm::length(glm::vec3(animator.pose_[leg.start][3]) - foot);
    rises[i] = std::clamp((to_model * glm::vec4(hit - flat, 0.0f)).y - standing,
                          -reach, reach);
    drop = std::min(drop, rises[i]);
    any = any || std::abs(rises[i]) > 1e-3f;
  }
  if (!any)
    return;
  std::vector<glm::vec3> lifts(rises.size());
  for (size_t i = 0; i < rises.size(); ++i)
    lifts[i] = glm::vec3(0.0f, rises[i] - drop, 0.0f);
  animator.sampler_->Plant(data, glm::vec3(0.0f, drop, 0.0f), lifts,
                           animator.pose_);
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

void Resample(AnimatorComponent& animator) {
  if (animator.data_ && animator.sampler_)
    _Sample(animator);
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
  ++_frame;
  if (ecs.Reg().storage<AnimatorComponent>().empty())
    return;
  Diagnostics::ScopedSection section("Animation");
  ecs.View<AnimatorComponent>().each([&](Entity entity,
                                         AnimatorComponent& animator) {
    if (!animator.data_ || !animator.playing_)
      return;
    animator.time_ += dt * animator.speed_;
    if (animator.fade_clip_ >= 0) {
      animator.fade_clip_time_ += dt * animator.speed_;
      animator.fade_elapsed_ += dt;
      if (animator.fade_elapsed_ >= animator.fade_length_)
        animator.fade_clip_ = -1;
    }
    // Far ones sample every few frames, each animator on frames of its own
    if (const auto* transform = ecs.Reg().try_get<TransformComponent>(entity)) {
      const AnimatorSettings& settings = animator.settings_;
      const float distance =
          Viewers::Distance(glm::vec3(transform->model_[3]));
      if (settings.sample_distance > 0.0f &&
          distance > settings.sample_distance)
        return;
      const int step = std::clamp(
          static_cast<int>(distance /
                           std::max(settings.full_rate_distance, 1e-3f)),
          1, std::max(settings.max_step, 1));
      if ((_frame + static_cast<uint32_t>(entity)) % step != 0)
        return;
    }
    _Sample(animator);
    _Plant(ecs, entity, animator);
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

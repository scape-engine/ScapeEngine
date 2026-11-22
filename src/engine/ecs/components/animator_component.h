#ifndef ANIMATOR_COMPONENT_H
#define ANIMATOR_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "engine/animation/skeleton.h"

using Entity = entt::entity;

/* ──────────────────────────────── ANIMATOR ─────────────────────────────── */

// Plays an animated model's clips: Animation::Update advances `time` and
// samples `pose`, which the skinned meshes naming this entity
// (MeshRendererComponent::animator_) follow
// TODO: editor inspector (clip, speed, play) and saving it with the scene
struct AnimatorComponent {
  // The model's, owned by its cache, which outlives the entity
  const SkeletalData* data_{nullptr};
  int clip_{0};  // index into the clips; out of range: the bind pose
  float time_{0.0f};
  float speed_{1.0f};
  bool playing_{true};
  bool loop_{true};

  // The clip fading out under `clip_` (Animation::Play; -1: none), its time,
  // and how long the fade lasts and has run
  int fade_clip_{-1};
  float fade_clip_time_{0.0f};
  float fade_length_{0.0f};
  float fade_elapsed_{0.0f};

  // The entity whose GroundComponent its feet stand on (none: as animated)
  Entity ground_{entt::null};

  // Its sampling buffers, and each skeleton node's model-space transform
  std::shared_ptr<PoseSampler> sampler_;
  std::vector<glm::mat4> pose_;
};

#endif  // ANIMATOR_COMPONENT_H

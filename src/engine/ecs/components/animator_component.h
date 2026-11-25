#ifndef ANIMATOR_COMPONENT_H
#define ANIMATOR_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "engine/animation/gait_set.h"
#include "engine/animation/skeleton.h"

using Entity = entt::entity;

/* ──────────────────────────────── ANIMATOR ─────────────────────────────── */

// How an animator samples its pose and plants its feet, by its distance from
// the viewer (Animation::SetViewer)
struct AnimatorSettings {
  bool plant_feet{true};
  float plant_distance{60.0f};  // feet plant within this of the viewer
  float plant_reach{0.5f};  // a foot moves at most this share of its leg
  // Farther than this, poses are sampled every few frames (one more per this
  // distance), up to every `max_step`th
  float full_rate_distance{20.0f};
  int max_step{8};
  // Farther than this, poses aren't sampled at all (the clips still run, so
  // they pick up where they'd be); 0: no limit
  float sample_distance{0.0f};
};

// Plays an animated model's clips: Animation::Update advances `time` and
// samples `pose`, which the skinned meshes naming this entity
// (MeshRendererComponent::animator_) follow
// TODO: save it with the scene
struct AnimatorComponent {
  // The model's, owned by its cache, which outlives the entity
  const SkeletalData* data_{nullptr};
  int clip_{0};  // index into the clips; out of range: the bind pose
  float time_{0.0f};
  float speed_{1.0f};
  bool playing_{true};
  bool loop_{true};
  AnimatorSettings settings_;
  // Additive pose layers on top of the clips (PoseLayers), each held at its
  // frame
  std::vector<PoseSampler::Layer> poses_;
  // The model's gaits (none: herds walk its walk clip)
  std::shared_ptr<GaitSet> gaits_;

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

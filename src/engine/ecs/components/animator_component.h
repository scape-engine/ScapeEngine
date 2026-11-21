#ifndef ANIMATOR_COMPONENT_H
#define ANIMATOR_COMPONENT_H

#include <glm/glm.hpp>
#include <vector>

#include "engine/animation/skeleton.h"

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

  // Each skeleton node's model-space transform
  std::vector<glm::mat4> pose_;
};

#endif  // ANIMATOR_COMPONENT_H

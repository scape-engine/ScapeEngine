#ifndef ANIMATION_H
#define ANIMATION_H

#include "engine/ecs/world.h"

struct AnimatorComponent;
struct SkeletalData;

namespace Animation {

// Gives `entity` an animator for `data` (left as it is when null or empty),
// posed at time 0. Skinned meshes follow it once their renderers name it
AnimatorComponent* AddAnimator(ECS& ecs, Entity entity,
                               const SkeletalData* data);

// Switches `animator` to clip `clip` from its start, fading the current one
// out over `fade` seconds (0: at once). The clip it already plays: no change
void Play(AnimatorComponent& animator, int clip, float fade = 0.0f);

// Where the scene is viewed from: animators plant their feet and sample at
// full rate by their distance from it (AnimatorSettings)
void SetViewer(const glm::vec3& position);
const glm::vec3& Viewer();

// Advances every playing animator by `dt` seconds and samples its pose
void Update(ECS& ecs, float dt);

// The pose a renderer's skinned mesh follows, and its node count; null when
// it names no animator
const glm::mat4* Pose(ECS& ecs, Entity animator, uint32_t& count);

}  // namespace Animation

#endif  // ANIMATION_H

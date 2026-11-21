#ifndef SKELETON_POSE_COMPONENT_H
#define SKELETON_POSE_COMPONENT_H

#include <glm/glm.hpp>
#include <vector>

#include "engine/renderer/model/skeleton.h"

// A model's animated pose: its nodes' model-space transforms, sampled every
// frame, which the skinned meshes under it follow (MeshRendererComponent's
// pose_ points at `nodes`, so its size stays fixed)
struct SkeletonPoseComponent {
  const Skeleton* skeleton = nullptr;
  const AnimationClip* clip = nullptr;  // none: the bind pose
  float phase = 0.0f;                   // seconds into the clip at time 0
  std::vector<glm::mat4> nodes;
};

#endif  // SKELETON_POSE_COMPONENT_H

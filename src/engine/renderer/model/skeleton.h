#ifndef SKELETON_H
#define SKELETON_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

// A model's node tree, flat: every parent before its children, so poses
// compose in one pass. Skinned meshes' bones are nodes of it
struct Skeleton {
  std::vector<std::string> names;
  std::vector<int> parents;  // -1: the root
  std::vector<glm::mat4> locals;

  int Find(const std::string& name) const;
};

// Keyframes per node, in seconds; a node without a channel keeps its local
// transform
struct AnimationClip {
  struct Channel {
    int node = -1;
    std::vector<float> position_times, rotation_times, scale_times;
    std::vector<glm::vec3> positions, scales;
    std::vector<glm::quat> rotations;
  };
  std::string name;
  float duration = 0.0f;
  std::vector<Channel> channels;
};

// Each node's model-space transform at `time` (looped over the clip; no clip:
// the bind pose)
void SamplePose(const Skeleton& skeleton, const AnimationClip* clip,
                float time, std::vector<glm::mat4>& globals);

#endif  // SKELETON_H

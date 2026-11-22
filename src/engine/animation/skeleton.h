#ifndef SKELETON_H
#define SKELETON_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace ozz::animation {
class Skeleton;
class Animation;
}  // namespace ozz::animation

// A model's node tree, flat: every parent before its children (depth-first,
// as ozz orders joints, so a node's index is its joint's). Skinned meshes'
// bones are nodes of it
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

// What an animated model brings: its skeleton and clips, and both as ozz
// samples them (BuildRuntime)
struct SkeletalData {
  Skeleton skeleton;
  std::vector<AnimationClip> clips;

  // Shared by copies; a clip that could not be built is null (bind pose)
  std::shared_ptr<const ozz::animation::Skeleton> runtime_skeleton;
  std::vector<std::shared_ptr<const ozz::animation::Animation>> runtime_clips;

  bool Empty() const { return skeleton.names.empty(); }
};

// Builds the ozz skeleton and clips from `skeleton` and `clips`; false (and
// none) when the tree does not fit ozz (over its joint limit)
bool BuildRuntime(SkeletalData& data);

// A clip's sampling point in [0, 1] at `time` (looped, else held at its ends)
float ClipRatio(const AnimationClip& clip, float time, bool loop);

// Samples a model's clips and blends them into one pose. Keeps ozz's
// sampling contexts and buffers from one frame to the next, so each animator
// has its own
class PoseSampler {
 public:
  struct Layer {
    int clip = -1;
    float ratio = 0.0f;  // ClipRatio
    float weight = 1.0f;
  };

  PoseSampler();
  ~PoseSampler();

  // Each node's model-space transform for `layers` blended by weight (none,
  // or all out of range: the bind pose)
  void Sample(const SkeletalData& data, const std::vector<Layer>& layers,
              std::vector<glm::mat4>& globals);

 private:
  struct Buffers;
  std::unique_ptr<Buffers> buffers_;
};

// Each node's model-space transform at `time` in clip `clip` (out of range:
// the bind pose). For tools and tests: animators sample through
// Animation::Update
void SamplePose(const SkeletalData& data, int clip, float time, bool loop,
                std::vector<glm::mat4>& globals);

#endif  // SKELETON_H

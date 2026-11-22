#include "skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>

namespace {

namespace offline = ozz::animation::offline;

ozz::math::Transform ToOzz(const glm::mat4& m) {
  glm::vec3 position, scale, skew;
  glm::quat rotation;
  glm::vec4 perspective;
  glm::decompose(m, scale, rotation, position, skew, perspective);
  ozz::math::Transform t;
  t.translation = ozz::math::Float3(position.x, position.y, position.z);
  t.rotation = ozz::math::Quaternion(rotation.x, rotation.y, rotation.z,
                                     rotation.w);
  t.scale = ozz::math::Float3(scale.x, scale.y, scale.z);
  return t;
}

// Keys in strictly rising time within the clip (a later key at the same time
// replaces the earlier one)
template <class Key, class Value, class Convert>
void AddKeys(const std::vector<float>& times, const std::vector<Value>& values,
             float duration, Convert convert, ozz::vector<Key>& keys) {
  for (size_t k = 0; k < times.size() && k < values.size(); ++k) {
    const float time = std::clamp(times[k], 0.0f, duration);
    if (!keys.empty() && keys.back().time >= time)
      keys.pop_back();
    keys.push_back({time, convert(values[k])});
  }
}

void Compose(const Skeleton& skeleton, std::vector<glm::mat4>& globals) {
  globals.resize(skeleton.locals.size());
  for (size_t i = 0; i < globals.size(); ++i) {
    const int parent = skeleton.parents[i];
    globals[i] = parent < 0 ? skeleton.locals[i]
                            : globals[parent] * skeleton.locals[i];
  }
}

}  // namespace

int Skeleton::Find(const std::string& name) const {
  const auto it = std::find(names.begin(), names.end(), name);
  return it == names.end() ? -1 : static_cast<int>(it - names.begin());
}

bool BuildRuntime(SkeletalData& data) {
  data.runtime_skeleton.reset();
  data.runtime_clips.clear();
  const Skeleton& skeleton = data.skeleton;
  const int count = static_cast<int>(skeleton.names.size());
  if (count == 0 || count > ozz::animation::Skeleton::kMaxJoints)
    return false;

  std::vector<std::vector<int>> children(count);
  std::vector<int> roots;
  for (int i = 0; i < count; ++i)
    (skeleton.parents[i] < 0 ? roots : children[skeleton.parents[i]])
        .push_back(i);
  const auto joint = [&](auto& self, int node) -> offline::RawSkeleton::Joint {
    offline::RawSkeleton::Joint out;
    out.name = skeleton.names[node].c_str();
    out.transform = ToOzz(skeleton.locals[node]);
    for (int child : children[node])
      out.children.push_back(self(self, child));
    return out;
  };
  offline::RawSkeleton raw;
  for (int root : roots)
    raw.roots.push_back(joint(joint, root));
  std::shared_ptr<const ozz::animation::Skeleton> built =
      offline::SkeletonBuilder()(raw);
  if (!built || built->num_joints() != count)
    return false;
  // Both depth-first in child order: a node's index is its joint's
  for (int i = 0; i < count; ++i)
    if (built->joint_parents()[i] != skeleton.parents[i])
      return false;

  for (const AnimationClip& clip : data.clips) {
    if (clip.duration <= 0.0f) {
      data.runtime_clips.push_back(nullptr);
      continue;
    }
    offline::RawAnimation animation;
    animation.duration = clip.duration;
    animation.tracks.resize(count);
    for (const AnimationClip::Channel& channel : clip.channels) {
      if (channel.node < 0 || channel.node >= count)
        continue;
      auto& track = animation.tracks[channel.node];
      AddKeys(channel.position_times, channel.positions, clip.duration,
              [](const glm::vec3& v) { return ozz::math::Float3(v.x, v.y, v.z); },
              track.translations);
      AddKeys(channel.rotation_times, channel.rotations, clip.duration,
              [](const glm::quat& q) {
                return ozz::math::Quaternion(q.x, q.y, q.z, q.w);
              },
              track.rotations);
      AddKeys(channel.scale_times, channel.scales, clip.duration,
              [](const glm::vec3& v) { return ozz::math::Float3(v.x, v.y, v.z); },
              track.scales);
    }
    // ozz reads a track without keys as identity: hold the node's own
    for (int i = 0; i < count; ++i) {
      auto& track = animation.tracks[i];
      const ozz::math::Transform rest = ToOzz(skeleton.locals[i]);
      if (track.translations.empty())
        track.translations.push_back({0.0f, rest.translation});
      if (track.rotations.empty())
        track.rotations.push_back({0.0f, rest.rotation});
      if (track.scales.empty())
        track.scales.push_back({0.0f, rest.scale});
    }
    data.runtime_clips.push_back(offline::AnimationBuilder()(animation));
  }
  data.runtime_skeleton = std::move(built);
  return true;
}

float ClipRatio(const AnimationClip& clip, float time, bool loop) {
  if (clip.duration <= 0.0f)
    return 0.0f;
  if (loop) {
    time = std::fmod(time, clip.duration);
    if (time < 0.0f)
      time += clip.duration;
  }
  return std::clamp(time / clip.duration, 0.0f, 1.0f);
}

struct PoseSampler::Buffers {
  struct Slot {
    ozz::animation::SamplingJob::Context context;
    ozz::vector<ozz::math::SoaTransform> locals;
  };
  std::vector<std::unique_ptr<Slot>> slots;  // one per layer
  ozz::vector<ozz::math::SoaTransform> blended;
  ozz::vector<ozz::math::Float4x4> models;
};

PoseSampler::PoseSampler() : buffers_(std::make_unique<Buffers>()) {}
PoseSampler::~PoseSampler() = default;

void PoseSampler::Sample(const SkeletalData& data,
                         const std::vector<Layer>& layers,
                         std::vector<glm::mat4>& globals) {
  const ozz::animation::Skeleton* skeleton = data.runtime_skeleton.get();
  if (!skeleton) {
    Compose(data.skeleton, globals);
    return;
  }
  Buffers& b = *buffers_;
  const int joints = skeleton->num_joints();
  const int soa = skeleton->num_soa_joints();

  std::vector<ozz::animation::BlendingJob::Layer> blend;
  for (const Layer& layer : layers) {
    const ozz::animation::Animation* animation =
        layer.clip >= 0 &&
                layer.clip < static_cast<int>(data.runtime_clips.size())
            ? data.runtime_clips[layer.clip].get()
            : nullptr;
    if (!animation || layer.weight <= 0.0f)
      continue;
    if (b.slots.size() <= blend.size())
      b.slots.push_back(std::make_unique<Buffers::Slot>());
    Buffers::Slot& slot = *b.slots[blend.size()];
    if (slot.context.max_tracks() < joints)
      slot.context.Resize(joints);
    slot.locals.resize(soa);
    ozz::animation::SamplingJob sampling;
    sampling.animation = animation;
    sampling.context = &slot.context;
    sampling.ratio = layer.ratio;
    sampling.output = ozz::make_span(slot.locals);
    sampling.Run();
    blend.push_back({});
    blend.back().weight = layer.weight;
    blend.back().transform = ozz::make_span(slot.locals);
  }

  // One full layer is the pose as it is; else the weighted mix (none: the
  // rest pose)
  b.blended.resize(soa);
  ozz::span<const ozz::math::SoaTransform> locals = ozz::make_span(b.blended);
  if (blend.size() == 1 && blend[0].weight >= 1.0f) {
    locals = blend[0].transform;
  } else {
    ozz::animation::BlendingJob blending;
    blending.layers = ozz::make_span(blend);
    blending.rest_pose = skeleton->joint_rest_poses();
    blending.output = ozz::make_span(b.blended);
    blending.Run();
  }

  b.models.resize(joints);
  ozz::animation::LocalToModelJob to_model;
  to_model.skeleton = skeleton;
  to_model.input = locals;
  to_model.output = ozz::make_span(b.models);
  to_model.Run();

  globals.resize(joints);
  for (int i = 0; i < joints; ++i)
    for (int c = 0; c < 4; ++c)
      ozz::math::StorePtrU(b.models[i].cols[c], &globals[i][c][0]);
}

void SamplePose(const SkeletalData& data, int clip, float time, bool loop,
                std::vector<glm::mat4>& globals) {
  PoseSampler sampler;
  const float ratio =
      clip >= 0 && clip < static_cast<int>(data.clips.size())
          ? ClipRatio(data.clips[clip], time, loop)
          : 0.0f;
  sampler.Sample(data, {{clip, ratio, 1.0f}}, globals);
}

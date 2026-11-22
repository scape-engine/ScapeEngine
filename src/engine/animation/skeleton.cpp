#include "skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/animation_optimizer.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/ik_two_bone_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/simd_quaternion.h>
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

bool BuildRuntime(SkeletalData& data, const std::vector<int>& posed) {
  data.runtime_skeleton.reset();
  data.runtime_clips.clear();
  data.runtime_nodes.clear();
  const Skeleton& skeleton = data.skeleton;
  const int count = static_cast<int>(skeleton.names.size());
  data.runtime_joints.assign(count, -1);

  // The posed nodes and their ancestors, in node order: depth-first in child
  // order as ozz orders its joints, so joint j is the j-th kept node
  std::vector<bool> kept(count, posed.empty());
  for (int node : posed)
    for (int n = node; n >= 0 && n < count && !kept[n]; n = skeleton.parents[n])
      kept[n] = true;
  std::vector<std::vector<int>> children(count);
  std::vector<int> roots;
  for (int i = 0; i < count; ++i) {
    if (!kept[i])
      continue;
    data.runtime_joints[i] = static_cast<int>(data.runtime_nodes.size());
    data.runtime_nodes.push_back(i);
    (skeleton.parents[i] < 0 ? roots : children[skeleton.parents[i]])
        .push_back(i);
  }
  const int joints = static_cast<int>(data.runtime_nodes.size());
  if (joints == 0 || joints > ozz::animation::Skeleton::kMaxJoints)
    return false;

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
  if (!built || built->num_joints() != joints)
    return false;
  for (int j = 0; j < joints; ++j) {
    const int parent = skeleton.parents[data.runtime_nodes[j]];
    if (built->joint_parents()[j] !=
        (parent < 0 ? -1 : data.runtime_joints[parent]))
      return false;
  }

  for (const AnimationClip& clip : data.clips) {
    if (clip.duration <= 0.0f) {
      data.runtime_clips.push_back(nullptr);
      continue;
    }
    offline::RawAnimation animation;
    animation.duration = clip.duration;
    animation.tracks.resize(joints);
    for (const AnimationClip::Channel& channel : clip.channels) {
      if (channel.node < 0 || channel.node >= count ||
          data.runtime_joints[channel.node] < 0)
        continue;
      auto& track = animation.tracks[data.runtime_joints[channel.node]];
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
    for (int j = 0; j < joints; ++j) {
      auto& track = animation.tracks[j];
      const ozz::math::Transform rest =
          ToOzz(skeleton.locals[data.runtime_nodes[j]]);
      if (track.translations.empty())
        track.translations.push_back({0.0f, rest.translation});
      if (track.rotations.empty())
        track.rotations.push_back({0.0f, rest.rotation});
      if (track.scales.empty())
        track.scales.push_back({0.0f, rest.scale});
    }
    // Keys the others interpolate to within 1 mm dropped (ozz's defaults)
    offline::RawAnimation optimized;
    if (!offline::AnimationOptimizer()(animation, *built, &optimized))
      optimized = std::move(animation);
    data.runtime_clips.push_back(offline::AnimationBuilder()(optimized));
  }
  data.runtime_skeleton = std::move(built);
  return true;
}

void FindLegs(SkeletalData& data, const std::vector<int>& bones) {
  data.legs.clear();
  const Skeleton& skeleton = data.skeleton;
  std::vector<glm::mat4> globals;
  Compose(skeleton, globals);
  std::vector<bool> bone(globals.size(), false);
  float low = 1e30f, high = -1e30f;
  for (int node : bones)
    if (node >= 0 && node < static_cast<int>(globals.size())) {
      bone[node] = true;
      low = std::min(low, globals[node][3].y);
      high = std::max(high, globals[node][3].y);
    }
  const float band = low + 0.1f * (high - low);
  const auto above = [&](int node) {
    return node >= 0 && bone[node] && globals[node][3].y > band;
  };
  for (int node = 0; node < static_cast<int>(globals.size()); ++node) {
    if (!bone[node] || globals[node][3].y > band)
      continue;
    const int mid = skeleton.parents[node];
    if (!above(mid) || !above(skeleton.parents[mid]))
      continue;
    data.legs.push_back({skeleton.parents[mid], mid, node});
  }
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

// The joints' model-space transforms into their nodes'; the nodes no mesh
// follows keep their bind transforms (set once)
static void _Store(const SkeletalData& data,
                   const ozz::vector<ozz::math::Float4x4>& models,
                   std::vector<glm::mat4>& globals) {
  if (globals.size() != data.skeleton.names.size())
    Compose(data.skeleton, globals);
  for (size_t j = 0; j < models.size(); ++j)
    for (int c = 0; c < 4; ++c)
      ozz::math::StorePtrU(models[j].cols[c],
                           &globals[data.runtime_nodes[j]][c][0]);
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
  if (blend.size() == 1 && blend[0].weight >= 1.0f) {
    b.blended.assign(blend[0].transform.begin(), blend[0].transform.end());
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
  to_model.input = ozz::make_span(b.blended);
  to_model.output = ozz::make_span(b.models);
  to_model.Run();

  _Store(data, b.models, globals);
}

// glm from an ozz column
static glm::vec3 _Point(const ozz::math::SimdFloat4& column) {
  float v[4];
  ozz::math::StorePtrU(column, v);
  return glm::vec3(v[0], v[1], v[2]);
}

static ozz::math::SimdFloat4 _Simd(const glm::vec3& v) {
  return ozz::math::simd_float4::Load(v.x, v.y, v.z, 0.0f);
}

void PoseSampler::Plant(const SkeletalData& data, const glm::vec3& shift,
                        const std::vector<glm::vec3>& lifts,
                        std::vector<glm::mat4>& globals) {
  const ozz::animation::Skeleton* skeleton = data.runtime_skeleton.get();
  Buffers& b = *buffers_;
  if (!skeleton || lifts.size() != data.legs.size() ||
      b.models.size() != static_cast<size_t>(skeleton->num_joints()))
    return;

  const ozz::math::SimdFloat4 move = _Simd(shift);
  for (ozz::math::Float4x4& model : b.models)
    model.cols[3] = model.cols[3] + move;

  for (size_t i = 0; i < data.legs.size(); ++i) {
    const Leg& node_leg = data.legs[i];
    const Leg leg{data.runtime_joints[node_leg.start],
                  data.runtime_joints[node_leg.mid],
                  data.runtime_joints[node_leg.end]};
    if (glm::dot(lifts[i], lifts[i]) < 1e-8f || leg.start < 0 ||
        leg.mid < 0 || leg.end < 0)
      continue;
    const glm::vec3 start = _Point(b.models[leg.start].cols[3]);
    const glm::vec3 mid = _Point(b.models[leg.mid].cols[3]);
    const glm::vec3 end = _Point(b.models[leg.end].cols[3]);
    // The knee's hinge: across the leg's plane, so a positive turn opens it
    // (in the knee's own space), and the way the knee points from the line
    // through hip and foot
    const glm::vec3 hinge = glm::cross(start - mid, end - mid);
    const glm::vec3 line = glm::normalize(end - start);
    const glm::vec3 pole =
        (mid - start) - line * glm::dot(mid - start, line);
    if (glm::dot(hinge, hinge) < 1e-10f || glm::dot(pole, pole) < 1e-10f)
      continue;  // a straight leg has no bend to follow
    glm::mat3 knee(_Point(b.models[leg.mid].cols[0]),
                   _Point(b.models[leg.mid].cols[1]),
                   _Point(b.models[leg.mid].cols[2]));

    ozz::math::SimdQuaternion start_turn, mid_turn;
    ozz::animation::IKTwoBoneJob ik;
    ik.target = _Simd(end + lifts[i]);
    ik.pole_vector = _Simd(glm::normalize(pole));
    ik.mid_axis = _Simd(glm::normalize(glm::inverse(knee) * hinge));
    ik.start_joint = &b.models[leg.start];
    ik.mid_joint = &b.models[leg.mid];
    ik.end_joint = &b.models[leg.end];
    ik.start_joint_correction = &start_turn;
    ik.mid_joint_correction = &mid_turn;
    if (!ik.Run())
      continue;

    // The turns on the joints' local rotations (one lane of a SoA group)
    for (const auto& [joint, turn] :
         {std::pair{leg.start, start_turn}, std::pair{leg.mid, mid_turn}}) {
      ozz::math::SoaTransform& group = b.blended[joint / 4];
      ozz::math::SimdQuaternion lanes[4];
      ozz::math::Transpose4x4(&group.rotation.x, &lanes->xyzw);
      lanes[joint & 3] = lanes[joint & 3] * turn;
      ozz::math::Transpose4x4(&lanes->xyzw, &group.rotation.x);
    }
    // The leg's joints and everything under them again, from the shifted
    // parent
    ozz::animation::LocalToModelJob to_model;
    to_model.skeleton = skeleton;
    to_model.input = ozz::make_span(b.blended);
    to_model.output = ozz::make_span(b.models);
    to_model.from = leg.start;
    to_model.Run();
  }

  _Store(data, b.models, globals);
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

float StrideSpeed(const SkeletalData& data, int clip, const glm::vec3& forward,
                  int samples) {
  if (clip < 0 || clip >= static_cast<int>(data.clips.size()) ||
      data.legs.empty() || data.clips[clip].duration <= 0.0f || samples < 2)
    return 0.0f;
  // Each foot's reach ahead over the cycle: the ground a planted foot
  // passes under the body in one stride
  PoseSampler sampler;
  std::vector<glm::mat4> pose;
  std::vector<float> low(data.legs.size(), INFINITY),
      high(data.legs.size(), -INFINITY);
  const AnimationClip& c = data.clips[clip];
  for (int i = 0; i < samples; ++i) {
    const float time = c.duration * i / samples;
    sampler.Sample(data, {{clip, ClipRatio(c, time, true), 1.0f}}, pose);
    for (size_t leg = 0; leg < data.legs.size(); ++leg) {
      const float z = glm::dot(glm::vec3(pose[data.legs[leg].end][3]), forward);
      low[leg] = std::min(low[leg], z);
      high[leg] = std::max(high[leg], z);
    }
  }
  float sum = 0.0f;
  for (size_t leg = 0; leg < data.legs.size(); ++leg)
    sum += high[leg] - low[leg];
  return sum / data.legs.size() / c.duration;
}

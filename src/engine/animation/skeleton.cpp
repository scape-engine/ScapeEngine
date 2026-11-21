#include "skeleton.h"

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

int Skeleton::Find(const std::string& name) const {
  const auto it = std::find(names.begin(), names.end(), name);
  return it == names.end() ? -1 : static_cast<int>(it - names.begin());
}

// The key before `time` and how far towards the next one
template <class T, class Mix>
static T Sample(const std::vector<float>& times, const std::vector<T>& values,
                float time, Mix mix) {
  if (values.size() == 1 || time <= times.front())
    return values.front();
  if (time >= times.back())
    return values.back();
  const size_t next =
      std::upper_bound(times.begin(), times.end(), time) - times.begin();
  const float span = times[next] - times[next - 1];
  const float t = span > 0.0f ? (time - times[next - 1]) / span : 0.0f;
  return mix(values[next - 1], values[next], t);
}

void SamplePose(const Skeleton& skeleton, const AnimationClip* clip,
                float time, bool loop, std::vector<glm::mat4>& globals) {
  std::vector<glm::mat4> locals = skeleton.locals;
  if (clip && clip->duration > 0.0f) {
    if (loop) {
      time = std::fmod(time, clip->duration);
      if (time < 0.0f)
        time += clip->duration;
    }
    for (const AnimationClip::Channel& channel : clip->channels) {
      if (channel.node < 0 || channel.node >= static_cast<int>(locals.size()))
        continue;
      // Channels without keys of a kind keep the bind pose's
      glm::vec3 position, scale, skew;
      glm::quat rotation;
      glm::vec4 perspective;
      glm::decompose(locals[channel.node], scale, rotation, position, skew,
                     perspective);
      const auto lerp = [](const glm::vec3& a, const glm::vec3& b, float t) {
        return glm::mix(a, b, t);
      };
      if (!channel.positions.empty())
        position =
            Sample(channel.position_times, channel.positions, time, lerp);
      if (!channel.scales.empty())
        scale = Sample(channel.scale_times, channel.scales, time, lerp);
      if (!channel.rotations.empty())
        rotation = Sample(channel.rotation_times, channel.rotations, time,
                          [](const glm::quat& a, const glm::quat& b, float t) {
                            return glm::slerp(a, b, t);
                          });
      locals[channel.node] = glm::translate(glm::mat4(1.0f), position) *
                             glm::mat4_cast(rotation) *
                             glm::scale(glm::mat4(1.0f), scale);
    }
  }
  globals.resize(locals.size());
  for (size_t i = 0; i < locals.size(); ++i) {
    const int parent = skeleton.parents[i];
    globals[i] = parent < 0 ? locals[i] : globals[parent] * locals[i];
  }
}

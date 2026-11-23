#include "pose_set.h"

#include <algorithm>
#include <cmath>

#include "content/creature_pose.h"

std::vector<PoseSampler::Layer> PoseLayers(const PoseSet& poses,
                                           const SkeletalData& data, int clip,
                                           uint64_t seed, float scale) {
  std::vector<PoseSampler::Layer> layers;
  const int anchors = static_cast<int>(poses.anchors.size());
  const int count = anchors + static_cast<int>(poses.items.size());
  if (clip < 0 || clip >= static_cast<int>(data.clips.size()) || count == 0 ||
      count > Worldgen::kPoseItemLimit)
    return layers;

  // Items by name, the last of a name winning
  std::vector<std::string> names;
  std::vector<float> spreads, values(count, 0.5f), deviates(count);
  for (const PoseSet::Anchor& anchor : poses.anchors) {
    names.push_back(anchor.name);
    spreads.push_back(anchor.spread);
  }
  for (const PoseSet::Item& item : poses.items) {
    names.push_back(item.name);
    spreads.push_back(item.spread);
  }
  const auto index = [&](const std::string& name) {
    const auto it = std::find(names.rbegin(), names.rend(), name);
    return it == names.rend() ? -1 : static_cast<int>(names.rend() - it) - 1;
  };
  std::vector<Worldgen::PoseLink> links;
  for (const PoseSet::Link& link : poses.links) {
    const int a = index(link.a), b = index(link.b);
    if (a >= 0 && b >= 0)
      links.push_back({a, b, link.correlation});
  }
  std::vector<float> correlation(Worldgen::kPoseItemLimit *
                                 Worldgen::kPoseItemLimit);
  Worldgen::PoseCorrelation(links.data(), static_cast<int>(links.size()),
                            correlation.data());

  for (int i = 0; i < anchors; ++i) {
    const PoseSet::Anchor& anchor = poses.anchors[i];
    if (anchor.source == "seed")
      values[i] = Worldgen::PoseWeight(seed, true);
    else if (anchor.source == "scale")
      values[i] = std::clamp(std::log2(scale) * 0.25f, 0.0f, 1.0f);
    else
      values[i] = anchor.value;
  }
  Worldgen::PoseValues(seed, true, deviates.data(), count);
  Worldgen::PoseBlend(anchors, count, spreads.data(), correlation.data(),
                      deviates.data(), values.data());

  // Each pose's frame by its value; an end past the clip, or not after the
  // start, is the clip's last frame
  const AnimationClip& source = data.clips[clip];
  const int last = static_cast<int>(std::lround(source.duration * 30.0f));
  for (size_t i = 0; i < poses.items.size(); ++i) {
    const PoseSet::Item& item = poses.items[i];
    if (item.start < 0 || source.duration <= 0.0f)
      continue;
    const int end = item.end > item.start && item.end <= last ? item.end : last;
    const float frame =
        static_cast<float>(end - item.start) * values[anchors + i] +
        static_cast<float>(item.start);
    layers.push_back(
        {clip, std::clamp(frame / 30.0f / source.duration, 0.0f, 1.0f), 1.0f,
         true});
  }
  return layers;
}

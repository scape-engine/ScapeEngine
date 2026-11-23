#ifndef POSE_SET_H
#define POSE_SET_H

#include <cstdint>
#include <string>
#include <vector>

#include "skeleton.h"

// A model's seeded shape: items drawn together from a seed (worldgen's
// PoseBlend), each with mean 0.5 and its own spread, tied by correlations.
// The anchors come first and are set, not drawn; each item with a frame range
// is a pose: a frame of `clip` between its start and end, by its value,
// played as an additive layer on top of the model's animation
struct PoseSet {
  struct Anchor {
    std::string name;
    // "seed": drawn from the seed; "scale": from the model's scale
    // (log2(scale) / 4, so scale 1 gives 0); else `value`
    std::string source;
    float value = 0.5f;
    float spread = 0.325f;  // std dev
  };
  struct Item {
    std::string name;
    int start = -1, end = -1;  // frames at 30 a second; none: no layer
    float spread = 0.325f;
  };
  // Correlation between two items (or anchors), -1..1
  struct Link {
    std::string a, b;
    float correlation = 0.0f;
  };

  std::string clip;
  std::vector<Anchor> anchors;
  std::vector<Item> items;
  std::vector<Link> links;
};

// The additive layers `poses` gives a model drawn from `seed` at `scale`:
// one per item with a frame range, of clip `clip` (an index into `data`'s
// clips, built additive)
std::vector<PoseSampler::Layer> PoseLayers(const PoseSet& poses,
                                           const SkeletalData& data, int clip,
                                           uint64_t seed, float scale);

#endif  // POSE_SET_H

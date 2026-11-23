#ifndef GAIT_SET_H
#define GAIT_SET_H

#include <string>
#include <vector>

// A model's gaits (its descriptor's "movement"): the clips it moves in, each
// covering `pace` metres a second at scale 1 when played at rate 1, and
// played no slower than min_rate and no faster than max_rate. Herds move a
// creature at their speed times `speed_scale`, in the gait that keeps up best
// (worldgen's PickGait)
struct GaitSet {
  struct Gait {
    std::string name;
    std::string clip;
    float pace = 0.0f;  // 0: measured from the clip's planted feet
    float min_rate = 0.3f;
    float max_rate = 1.3f;
    int clip_index = -1;  // into the model's clips; -1: missing
  };
  float speed_scale = 1.0f;
  std::vector<Gait> gaits;
};

#endif  // GAIT_SET_H

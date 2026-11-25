#ifndef ROTATION_JITTER_H
#define ROTATION_JITTER_H

#include <glm/vec3.hpp>

#include "engine/pcg/pipeline/operation_data.h"
#include "engine/pcg/pipeline/operation_registry.h"

namespace ProcModel {

// TODO: use Transform operations and Transformation system instead of glm
// TODO: move parsing logic out of operation code?

// Per-locator rotation perturbation. For each locator, draws a uniform
// random angle from [pitch_min, pitch_max], [yaw_min, yaw_max],
// [roll_min, roll_max] and composes it into the locator's frame.
// Authoring shorthand: a single "pitch": N value expands to
// [-N, +N]. Same authored range produces different perturbations
// per locator via independent RNG draws.
struct RotationJitterData : public PCG::OperationData {
  // Stored in radians; parser converts from degrees.
  // x = pitch, y = yaw, z = roll
  glm::vec3 min = glm::vec3(0.0f);
  glm::vec3 max = glm::vec3(0.0f);
};

void RegisterRotationJitterOperation(PCG::OperationRegistry& registry);

}  // namespace ProcModel

#endif  // ROTATION_JITTER_H
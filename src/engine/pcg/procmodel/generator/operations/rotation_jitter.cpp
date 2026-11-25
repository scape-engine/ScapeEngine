#include "rotation_jitter.h"

#include <nlohmann/json.hpp>
#include <random>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "engine/pcg/procmodel/generator/locator_context.h"

namespace ProcModel {

static std::shared_ptr<PCG::OperationData> ParseRotationJitter(
    const nlohmann::json& params) {
  auto data = std::make_shared<RotationJitterData>();

  // Per-axis range. Explicit "<axis>_min"/"<axis>_max" take precedence;
  // shorthand "<axis>: N" expands to [-N, +N]. Missing axis = no jitter.
  auto parse_axis = [&](const std::string& name, float& dst_min,
                        float& dst_max) {
    const std::string kmin = name + "_min";
    const std::string kmax = name + "_max";
    if (params.contains(kmin) || params.contains(kmax)) {
      dst_min = params.value(kmin, 0.0f);
      dst_max = params.value(kmax, 0.0f);
    } else if (params.contains(name)) {
      const float r = params.value(name, 0.0f);
      dst_min = -r;
      dst_max = r;
    }
  };

  float pitch_min = 0.0f, pitch_max = 0.0f;
  float yaw_min = 0.0f, yaw_max = 0.0f;
  float roll_min = 0.0f, roll_max = 0.0f;
  parse_axis("pitch", pitch_min, pitch_max);
  parse_axis("yaw", yaw_min, yaw_max);
  parse_axis("roll", roll_min, roll_max);

  data->min = glm::radians(glm::vec3(pitch_min, yaw_min, roll_min));
  data->max = glm::radians(glm::vec3(pitch_max, yaw_max, roll_max));

  return data;
}

static void ApplyRotationJitter(const PCG::OperationData& base_data,
                                PCG::OperationContext& base_ctx) {
  const auto& data = PCG::OperationCast<RotationJitterData>(base_data);
  auto& ctx = PCG::OperationCast<LocatorContext>(base_ctx);

  const float pitch = ctx.sampler.Uniform("rotation_jitter", "x", data.min.x,
                                          data.max.x, ctx.rng);
  const float yaw = ctx.sampler.Uniform("rotation_jitter", "y", data.min.y,
                                        data.max.y, ctx.rng);
  const float roll = ctx.sampler.Uniform("rotation_jitter", "z", data.min.z,
                                         data.max.z, ctx.rng);

  // Compose as pitch (X) → yaw (Y) → roll (Z) intrinsic rotations.
  // Post-multiplied into locator_world so jitter applies in the locator's
  // own local frame; pre-multiply would jitter in world space, which
  // would defeat radial_align if listed after it.
  glm::mat4 jitter(1.0f);
  jitter = glm::rotate(jitter, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
  jitter = glm::rotate(jitter, pitch, glm::vec3(1.0f, 0.0f, 0.0f));
  jitter = glm::rotate(jitter, roll, glm::vec3(0.0f, 0.0f, 1.0f));

  ctx.locator_world = ctx.locator_world * jitter;
}

void RegisterRotationJitterOperation(PCG::OperationRegistry& registry) {
  registry.Register("rotation_jitter", &ParseRotationJitter,
                    &ApplyRotationJitter);
}

}  // namespace ProcModel
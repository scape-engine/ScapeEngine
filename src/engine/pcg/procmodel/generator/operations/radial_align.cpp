#include "radial_align.h"

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

#include "engine/core/logger.h"

#include "engine/pcg/procmodel/generator/locator_context.h"

namespace ProcModel {

static std::shared_ptr<PCG::OperationData> ParseRadialAlign(
    const nlohmann::json& /*params*/) {
  // No params for now. Future: optional up_axis override, optional
  // up-vector source (activator's local Y vs world Y).
  return std::make_shared<RadialAlignData>();
}

static void ApplyRadialAlign(const PCG::OperationData& /*data*/,
                             PCG::OperationContext& base_ctx) {
  auto& ctx = PCG::OperationCast<LocatorContext>(base_ctx);

  const glm::vec3 locator_pos(ctx.locator_world[3]);
  const glm::vec3 activator_pos(ctx.activator_world[3]);

  // Outward direction is the horizontal vector from activator to locator.
  // Projecting onto the XZ plane keeps the locator's vertical alignment
  // independent of where it sits along the activator's vertical axis.
  glm::vec3 outward = locator_pos - activator_pos;
  outward.y = 0.0f;
  const float len = glm::length(outward);
  if (len < 1e-4f) {
    // locator sits on the activator's vertical axis; no defined outward.
    // Leave the frame untouched rather than synthesize an arbitrary one.
    return;
  }
  outward /= len;

  const glm::vec3 up(0.0f, 1.0f, 0.0f);
  const glm::vec3 right = glm::normalize(glm::cross(up, outward));

  // Rebuild the rotation: +X = right (tangent), +Y = up (vertical),
  // +Z = outward. Position (column 3) and scale (the columns' lengths, which
  // the parts placed on it take) are preserved.
  const glm::vec3 axes[3] = {right, up, outward};
  for (int c = 0; c < 3; ++c)
    ctx.locator_world[c] =
        glm::vec4(axes[c] * glm::length(glm::vec3(ctx.locator_world[c])), 0.0f);
}

void RegisterRadialAlignOperation(PCG::OperationRegistry& registry) {
  registry.Register("radial_align", &ParseRadialAlign, &ApplyRadialAlign);
}

}  // namespace ProcModel
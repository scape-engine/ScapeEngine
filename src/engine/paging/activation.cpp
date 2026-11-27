#include "activation.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "engine/ecs/components/activation_component.h"
#include "engine/ecs/components/transform_component.h"
#include "engine/paging/viewers.h"

namespace Activation {

// An active entity stays so until this much farther, so one at the edge
// doesn't switch every frame
static constexpr float slack = 1.1f;

void Update(ECS& ecs) {
  struct Candidate {
    Entity entity;
    float distance;
  };
  // Those in range, by parent, for the caps
  std::unordered_map<Entity, std::vector<Candidate>> in_range;
  for (auto [entity, transform, activation] :
       ecs.View<TransformComponent, ActivationComponent>().each()) {
    if (activation.distance <= 0.0f) {
      activation.active_ = true;
      continue;
    }
    // World space, from the cached matrix (works on any ECS, not only Main)
    const float distance = std::max(
        0.0f, Viewers::Distance(glm::vec3(transform.model_[3])) -
                  activation.extent);
    const float reach =
        activation.distance * (activation.active_ ? slack : 1.0f);
    activation.active_ = false;
    if (distance <= reach)
      in_range[transform.parent_].push_back({entity, distance});
  }
  for (auto& [parent, candidates] : in_range) {
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                return a.distance < b.distance;
              });
    for (size_t i = 0; i < candidates.size(); ++i) {
      auto& activation = ecs.Get<ActivationComponent>(candidates[i].entity);
      activation.active_ =
          activation.max_active == 0 || i < activation.max_active;
    }
  }
}

}  // namespace Activation

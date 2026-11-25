#include "viewers.h"

#include <limits>

#include "engine/ecs/components/transform_component.h"
#include "engine/ecs/components/viewer_component.h"
#include "engine/transform/transform.h"

namespace Viewers {

static std::optional<glm::vec3> s_host;
static std::vector<glm::vec3> s_positions;

void Update(ECS& ecs) {
  s_positions.clear();
  if (s_host)
    s_positions.push_back(*s_host);
  auto view = ecs.View<TransformComponent, ViewerComponent>();
  for (const Entity entity : view)
    s_positions.push_back(Transform::GetPosition(
        view.template get<TransformComponent>(entity), Space::WORLD));
}

void SetHost(std::optional<glm::vec3> position) {
  s_host = position;
}

const std::vector<glm::vec3>& Positions() {
  return s_positions;
}

glm::vec3 Nearest(const glm::vec3& point) {
  glm::vec3 nearest(0.0f);
  float best = std::numeric_limits<float>::infinity();
  for (const glm::vec3& position : s_positions)
    if (const float d = glm::distance(point, position); d < best) {
      best = d;
      nearest = position;
    }
  return nearest;
}

float Distance(const glm::vec3& point) {
  float best = std::numeric_limits<float>::infinity();
  for (const glm::vec3& position : s_positions)
    best = std::min(best, glm::distance(point, position));
  return best;
}

}  // namespace Viewers

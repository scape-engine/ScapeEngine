#ifndef VIEWERS_H
#define VIEWERS_H

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "engine/ecs/world.h"

// Where the world is viewed from: every entity with a ViewerComponent, and
// the host's own viewer when it has one (the editor's camera, which isn't in
// the scene). Systems load, pick detail and simulate around these
namespace Viewers {

// Gathers this frame's viewers; once a frame, before the systems use them
void Update(ECS& ecs);

// The host's viewer (world space); nullopt: none
void SetHost(std::optional<glm::vec3> position);

// This frame's viewers, world space
const std::vector<glm::vec3>& Positions();

// The viewer nearest the point; the origin when there is none
glm::vec3 Nearest(const glm::vec3& point);

// From the point to the nearest viewer; infinite when there is none
float Distance(const glm::vec3& point);

}  // namespace Viewers

#endif  // VIEWERS_H

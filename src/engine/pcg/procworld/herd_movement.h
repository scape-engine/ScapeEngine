#ifndef HERD_MOVEMENT_H
#define HERD_MOVEMENT_H

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

#include "engine/ecs/components/animator_component.h"
#include "engine/ecs/world.h"

#include "placement/nav_mesh.h"

namespace ProcWorld {

// How a planet's creatures roam: each rests, then walks along its region's
// navmesh to a point near where it was placed, and rests again
struct HerdSettings {
  bool roam{true};
  // The speed every creature heads for, times its model's gait speed scale
  float walk_speed{1.0f};    // metres per second
  float turn_rate{120.0f};   // degrees per second
  float roam_radius{20.0f};  // walks end this near where it was placed
  float rest_min{4.0f};      // seconds between walks
  float rest_max{12.0f};
  float nav_reach{4.0f};       // points find navmesh polygons this near
  float roam_distance{150.0f};  // only creatures this near the viewer roam
  // The clips played walking and resting: the first whose name holds this,
  // in any case; none: the clip is left as it is
  std::string walk_clip{"walk"};
  std::string rest_clip{"idle"};
  float fade{0.3f};  // seconds a clip fades into the next, at rate 1
  // The axis the models face along, in model space; zero: each model's own,
  // the way its walk clip walks (StrideVelocity)
  glm::vec3 forward{0.0f};
  // Gaits: each creature plays the one of its model's gaits that comes
  // nearest its speed (PickGait), at a rate within the gait's own; a model
  // with none has one, its walk clip at the pace its planted feet keep,
  // within these rates. Off: the first gait's clip (or the walk clip) at
  // speed 1, moving at the speed
  bool match_stride{true};
  float walk_min_rate{1.0f};
  float walk_max_rate{1.0f};
  // A creature moves as fast as its gait reaches, so its feet keep pace with
  // the ground; off: at its speed, its clip as near as its rates allow
  bool keep_pace{true};
  AnimatorSettings animation;  // the creatures' animators
};

// On a herd entity: its region's navmesh (in the planet's space), where the
// herd's origin is in that space, and how its creatures roam
struct HerdComponent {
  std::shared_ptr<const Worldgen::NavMesh::PathfinderMesh> path_;
  glm::vec3 origin_{0.0f};
  HerdSettings settings_;
};

// On a creature of a herd (a child of its entity): where it was placed, its
// height over the herd's ground there, the corners still to walk to (in the
// herd's space) and how long it still rests
struct RoamComponent {
  glm::vec3 home_{0.0f};
  float height_{0.0f};
  std::vector<glm::vec3> waypoints_;
  float rest_{0.0f};
  uint32_t random_{1};
};

namespace HerdMovement {

// Moves the roaming creatures of every herd by `dt` seconds, before their
// animators sample
void Update(ECS& ecs, float dt);

}  // namespace HerdMovement

}  // namespace ProcWorld

#endif  // HERD_MOVEMENT_H

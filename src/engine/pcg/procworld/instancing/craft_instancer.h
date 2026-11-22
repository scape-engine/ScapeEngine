#ifndef CRAFT_INSTANCER_H
#define CRAFT_INSTANCER_H

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "engine/ecs/world.h"
#include "engine/pcg/procmodel/procmodel.h"

#include "api/craft.h"

namespace ProcWorld {

// Makes a fleet's entities in three steps, so only the last runs on the main
// thread and it can be spread over frames: Plan gives each craft its model (a
// source as RegionInstancer takes) and a point and heading its seed gives
// inside the bounds, with its procmodel's archetype when it is loaded (the
// caller reads the others, ProcModel::Subsystem::ReadArchetype); Generate
// picks a procmodel craft's parts from its seed (any thread); Make creates
// its entity as a child of the source (the volume entity), tagged with
// ProceduralInstanceComponent
// TODO: craft can overlap; space them by their models' bounds
class CraftInstancer {
public:
  struct CraftPlan {
    std::string model;  // the source, as the content names it
    uint64_t seed = 0;
    glm::vec3 position{0.0f};
    float heading = 0.0f;  // degrees about +y
    // A procmodel's: its descriptor path and loaded archetype
    std::string descriptor;
    std::shared_ptr<const ProcModel::ProcModelResource> archetype;
  };

  // Slots no model fits are left out and counted in `missing`
  static std::vector<CraftPlan> Plan(
      const Worldgen::CraftContent& content,
      const std::vector<Worldgen::FleetEntry>& fleet,
      const glm::vec3& bounds_min, const glm::vec3& bounds_max,
      const std::filesystem::path& root, size_t& missing);

  // A procmodel craft's parts; none for other sources or a failed pick
  static std::optional<ProcModel::InstanceData> Generate(
      const CraftPlan& plan);

  // entt::null if it can't be made (a procmodel without its archetype)
  static Entity Make(ECS& ecs, const CraftPlan& plan,
                     const std::optional<ProcModel::InstanceData>& instance,
                     Entity source, const std::filesystem::path& root);
};

}  // namespace ProcWorld

#endif  // CRAFT_INSTANCER_H

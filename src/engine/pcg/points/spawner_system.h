#ifndef PCG_SPAWNER_SYSTEM_H
#define PCG_SPAWNER_SYSTEM_H

#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/ecs/components/volume_component.h"
#include "engine/ecs/world.h"
#include "engine/pcg/points/point_source.h"

namespace PCG {

class SpawnerGenerator;

// Runs the volumes whose generator is a SpawnerGenerator: a dirty one's
// points and template profile are read on a worker, then each point gets a
// child volume with a generator of its own (the profile with the point's
// attributes bound in), which the other systems run as any volume's
class SpawnerSystem {
public:
  // Called from PCGEngine::ProcessQueued, before the systems running what it
  // spawns
  void Update();
  // Takes down what it spawned
  void Shutdown();

private:
  struct Build {
    std::vector<Point> points;
    nlohmann::json profile;
    std::string error;
    float ms = 0.0f;
  };
  struct Spawned {
    Entity entity = entt::null;
    ResourceID generator = 0;
  };
  struct State {
    std::future<Build> building;
    std::vector<Spawned> spawned;
  };

  void Spawn(Entity volume, SpawnerGenerator& spawner, const Build& build,
             State& state);
  // Destroys its spawned volumes and releases their generators
  static void Clear(State& state);

  std::unordered_map<Entity, State> volumes_;
};

}  // namespace PCG

#endif  // PCG_SPAWNER_SYSTEM_H

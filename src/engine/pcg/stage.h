#ifndef PCG_STAGE_H
#define PCG_STAGE_H

#include <string>

#include <nlohmann/json.hpp>

namespace PCG {

// What a stage outputs, after UE's PCG nodes
enum class StageType {
  Surface,          // a mesh over its grid's cells
  InstanceSpawner,  // instanced meshes, one per point (Static Mesh Spawner)
  EntitySpawner,    // an entity with components per point (Spawn Actor)
};

const char* StageTypeName(StageType type);

// One step of a generator: what it outputs, on which grid and how far from
// the viewers. A generator's stages add their own settings to these
struct Stage {
  StageType type = StageType::Surface;
  // Its display name, in the generator's stage list and its entities' path
  std::string name;
  bool enabled = true;
  // The level of the generator's grid it generates on; -1: none of its own
  int grid = -1;
  // How far from the viewers it generates, in metres; 0: no limit of its own
  float generation_radius = 0.0f;
};

// Its common settings; the type is the generator's to give
nlohmann::json WriteStage(const Stage& stage);
// Missing values keep what the stage has
void ReadStage(const nlohmann::json& in, Stage& stage);

}  // namespace PCG

#endif  // PCG_STAGE_H

#include "stage.h"

namespace PCG {

const char* StageTypeName(StageType type) {
  switch (type) {
    case StageType::Surface:
      return "Surface";
    case StageType::InstanceSpawner:
      return "Instance Spawner";
    case StageType::EntitySpawner:
      return "Entity Spawner";
  }
  return "";
}

nlohmann::json WriteStage(const Stage& stage) {
  return {{"label", stage.label},
          {"enabled", stage.enabled},
          {"grid", stage.grid},
          {"generation_radius", stage.generation_radius}};
}

void ReadStage(const nlohmann::json& in, Stage& stage) {
  stage.label = in.value("label", stage.label);
  stage.enabled = in.value("enabled", stage.enabled);
  stage.grid = in.value("grid", stage.grid);
  stage.generation_radius =
      in.value("generation_radius", stage.generation_radius);
}

}  // namespace PCG

#ifndef PLANET_GENERATOR_H
#define PLANET_GENERATOR_H

#include <string>

#include "engine/pcg/generator_base.h"

namespace ProcWorld {

// A volume's worldgen planet: regions of its content (JSON, project-relative)
// load around the viewer, finer the closer they are. The planet's centre is
// the volume entity
class PlanetGenerator : public PCG::Generator {
public:
  PCG::GeneratorType GetType() const override {
    return PCG::GeneratorType::Planet;
  }
  std::string GetDisplayName() const override { return "Planet"; }
  std::unique_ptr<PCG::Generator> Clone() const override {
    return std::make_unique<PlanetGenerator>(*this);
  }

  std::string content_path;
  int finest_level = 0;
  int coarsest_level = 5;
  // A region splits into four finer ones when the viewer is closer than this
  // many times its width
  float split_distance = 2.0f;
  // Objects are placed on regions up to this level
  int object_level = 0;
  // Regions building at once on worker threads, nearest first; 0: one per
  // core but one
  int worker_jobs = 0;

  // State, written by ProcWorld::Subsystem
  std::string error;
  size_t loaded_regions = 0;
  size_t queued_regions = 0;
  size_t building_regions = 0;
};

}  // namespace ProcWorld

#endif  // PLANET_GENERATOR_H

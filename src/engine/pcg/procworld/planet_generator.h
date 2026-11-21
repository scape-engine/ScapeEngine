#ifndef PLANET_GENERATOR_H
#define PLANET_GENERATOR_H

#include <cstdint>
#include <string>

#include "engine/pcg/generator_base.h"
#include "engine/pcg/procworld/terrain_look.h"

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
  // Regions farther than this from the viewer are not loaded; 0: no limit
  float view_distance = 8192.0f;
  // Objects are placed on regions up to this level
  int object_level = 0;
  // Creature herds come from the regions of this level over the loaded
  // regions this fine or finer (the game's: 1); -1: no herds
  int herd_level = 1;
  // Regions building at once on worker threads, nearest first; 0: one per
  // core but one
  int worker_jobs = 0;
  // The terrain's tile colours and material settings
  TerrainMaterialData look = DefaultTerrainLook();
  // The planet itself, optional: worldgen planet content (JSON,
  // project-relative) built from the seed, as the given biome and subtype.
  // Its terrain settings (when it has terrain types) replace the region
  // content's, its radius (when it has a size table) the content's radius,
  // and the tile colours it gives replace the look's
  std::string planet_path;
  uint64_t seed = 1;
  int biome = 0;
  int subtype = 0;
  int size = 0;  // the size class

  // State, written by ProcWorld::Subsystem
  std::string error;
  size_t loaded_regions = 0;
  size_t queued_regions = 0;
  size_t building_regions = 0;
  int planet_tile_colours = 0;  // tiles coloured by the planet
  bool planet_terrain = false;  // the terrain is the planet's
  float planet_radius = 0.0f;   // from the size class; 0: the content's
};

}  // namespace ProcWorld

#endif  // PLANET_GENERATOR_H

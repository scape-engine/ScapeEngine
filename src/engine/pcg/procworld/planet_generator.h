#ifndef PLANET_GENERATOR_H
#define PLANET_GENERATOR_H

#include <cstdint>
#include <string>

#include "engine/pcg/generator_base.h"
#include "engine/pcg/procworld/herd_movement.h"
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
  // Buildings are placed on regions up to this level; -1: none
  int building_level = 9;
  // Creature herds come from the regions of this level over the loaded
  // regions this fine or finer (the game's: 1); -1: no herds
  int herd_level = 1;
  // How the creatures roam and animate; edits apply from the next frame
  HerdSettings creatures;
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
  // Testing: the water acts as though a weather event is under way
  bool force_weather_event = false;

  // State, written by ProcWorld::Subsystem
  std::string error;
  size_t loaded_regions = 0;
  size_t queued_regions = 0;
  size_t building_regions = 0;
  int planet_tile_colours = 0;  // tiles coloured by the planet
  bool planet_terrain = false;  // the terrain is the planet's
  float planet_radius = 0.0f;   // from the size class; 0: the content's
  // The weather event at the world clock, from the planet's schedule
  // TODO: the renderer draws the event from these (fog, particles, sky,
  // sound), within a frame budget
  bool weather_events = false;         // the planet has a schedule
  bool weather_event = false;          // one is under way: warning to end
  bool weather_event_warning = false;  // between its warning and its start
  int weather_event_kind = -1;         // -1: none under way, or no kinds
  float weather_event_intensity = 0.0f;
  // 0 clear to 1 full: rising from its start, falling from its fade
  float weather_event_strength = 0.0f;
  // Seconds to the event's next stage, else to the next event; -1: none
  double weather_event_change = -1.0;
  // The water's state (an index into the planet content's water states) at
  // the world clock, and the one it blends from; -1: the planet content has
  // none
  // TODO: the water surface reads these once Scape draws water
  int water_state = -1;
  int water_state_from = -1;
  float water_blend = 1.0f;  // 0: all `water_state_from`, 1: all `water_state`
};

}  // namespace ProcWorld

#endif  // PLANET_GENERATOR_H

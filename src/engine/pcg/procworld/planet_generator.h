#ifndef PLANET_GENERATOR_H
#define PLANET_GENERATOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "engine/pcg/generator_base.h"
#include "engine/pcg/stage.h"
#include "engine/pcg/procworld/herd_movement.h"
#include "engine/pcg/procworld/terrain_look.h"
#include "engine/pcg/water/water_subsystem.h"

namespace ProcWorld {

// The planet's grid: level L's cells are 32 << L metres wide

// The terrain's regions: a quadtree from `grid` (the finest level) to
// coarsest_level, out to the generation radius
struct TerrainStage : PCG::Stage {
  int coarsest_level = 5;
  // A region splits into four finer ones when the viewer is closer than this
  // many times its width
  float split_distance = 2.0f;
};

// Object kinds (.object assets, project-relative) placed after the region
// content's own, each by the cells of its placement size and as far as it
// draws (its fade end), within the generation radius
struct ObjectStage : PCG::Stage {
  std::vector<std::string> assets;
};

// The herds: an entity per creature, from the cells of `grid` over the
// terrain regions that fine or finer
struct HerdStage : PCG::Stage {
  // How the creatures roam and animate; edits apply from the next frame
  HerdSettings creatures;
};

// The sea's surface (planets whose terrain has a sea level), at the radius
// plus the sea level; its waves come from the planet content
struct SeaStage : PCG::Stage {
  Water::SurfaceSettings water;
};

// A volume's worldgen planet: regions of its content (JSON, project-relative)
// load around the viewers, finer the closer they are, and its stages make
// them into terrain, buildings, objects, herds and a sea. The planet's centre
// is the volume entity
class PlanetGenerator : public PCG::Generator {
public:
  PCG::GeneratorType GetType() const override {
    return PCG::GeneratorType::Planet;
  }
  std::string GetDisplayName() const override { return "Planet"; }
  std::unique_ptr<PCG::Generator> Clone() const override {
    return std::make_unique<PlanetGenerator>(*this);
  }
  PCG::Stage* GetStage(const std::string& name) override {
    if (name == "terrain")
      return &terrain;
    if (name == "buildings")
      return &buildings;
    if (name == "objects")
      return &objects;
    if (name == "herds")
      return &herds;
    if (name == "sea")
      return &sea;
    return nullptr;
  }

  std::string content_path;
  // Its stages. The names are the user's; grids and radii as each says
  TerrainStage terrain{{PCG::StageType::Surface, "Terrain", true, 0, 8192.0f}};
  // Buildings, on the terrain's regions of `grid` or finer
  PCG::Stage buildings{PCG::StageType::InstanceSpawner, "Buildings", true, 9,
                       0.0f};
  ObjectStage objects{{PCG::StageType::InstanceSpawner, "Objects"}};
  // The game's herd level: 1
  HerdStage herds{{PCG::StageType::EntitySpawner, "Creatures", true, 1}};
  SeaStage sea{{PCG::StageType::Surface, "Sea"}};
  // Regions building at once on worker threads, nearest first; 0: one per
  // core but one
  int worker_jobs = 0;
  // Seconds its regions' terrain, buildings, objects and creatures take to
  // fade in as they load and out as they unload (FadeComponent); 0: at once
  float fade_time = 0.5f;
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
  int water_state = -1;
  int water_state_from = -1;
  float water_blend = 1.0f;  // 0: all `water_state_from`, 1: all `water_state`
};

}  // namespace ProcWorld

#endif  // PLANET_GENERATOR_H

#ifndef PROCWORLD_H
#define PROCWORLD_H

#include <deque>
#include <cstdint>
#include <filesystem>
#include <future>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <unordered_map>

#include "engine/ecs/world.h"
#include "engine/paging/cell_jobs.h"
#include "engine/pcg/edit_layer.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/material/terrain_material.h"
#include "engine/renderer/model/mesh.h"

#include "api/procedural_texture.h"
#include "water/water_waves.h"
#include "world/planet_water.h"
#include "world/planet_weather_events.h"
#include "api/region.h"
#include "engine/pcg/procworld/instancing/building_library.h"
#include "engine/pcg/procworld/instancing/craft_instancer.h"
#include "engine/pcg/procworld/instancing/region_instancer.h"

namespace ProcWorld {

class RegionGenerator;
class PlanetGenerator;
class CraftGenerator;

// Builds worldgen terrain for the volumes whose generator is a
// RegionGenerator (one region) or a PlanetGenerator (regions around the
// viewer), and craft for a CraftGenerator: terrain, objects and craft become
// child entities of the volume entity
class Subsystem {
public:
  Subsystem() = default;
  ~Subsystem() = default;

  void Init();
  void Shutdown();

  // Rebuilds the dirty volumes and loads planet regions around the viewer.
  // Called from PCGEngine::ProcessQueued()
  void Update();

  // Content paths and object sources are relative to it
  void SetRoot(const std::filesystem::path& root) { root_ = root; }
  const std::filesystem::path& Root() const { return root_; }

private:
  struct LoadedRegion {
    Entity entity = entt::null;
    std::unique_ptr<Mesh> terrain_mesh;
  };
  // A region built on a worker, with its buildings' pieces
  struct RegionBuild {
    Worldgen::Region region;
    RegionInstancer::BuildingPieces pieces;
    // Its procmodel objects' (and creatures') instances, generated on the
    // worker so the main thread only makes their entities
    RegionInstancer::ObjectModels objects;
    // Its objects' seeds and keys, for an object cell; empty:
    // RegionInstancer::Tags'
    RegionInstancer::ObjectTags tags;
    // The edits it was built with, and those whose objects the rules no
    // longer place there
    std::optional<PCG::CellEdits> edits;
    size_t orphan_edits = 0;
  };
  struct PlanetState {
    // Shared with the jobs building from it, so a reload leaves them intact
    std::shared_ptr<const Worldgen::RegionContent> content;
    std::unordered_map<uint64_t, LoadedRegion> regions;  // by cell key
    // Unloaded regions fading out: kept until their entity is gone, as it
    // draws their terrain mesh
    std::vector<LoadedRegion> leaving;
    Paging::CellJobs<RegionBuild> building;
    // Herds come from regions of the herd level and objects from regions of
    // their placement sizes, apart from the terrain (whose level changes
    // under them): the herds' content has no objects, the objects' no herds
    // and the terrain's neither
    std::shared_ptr<const Worldgen::RegionContent> herd_content;
    std::unordered_map<uint64_t, Entity> herds;  // by cell key
    Paging::CellJobs<RegionBuild> herd_building;
    std::shared_ptr<const Worldgen::RegionContent> object_content;
    // The levels some object kind's size is, and how far from the viewer its
    // kinds draw (their farthest fade end; infinite: one has no limit)
    struct ObjectLevel {
      int level;
      float reach;
    };
    std::vector<ObjectLevel> object_levels;
    // An object cell's entity and the sphere round its objects' origins
    // (radius < 0: it has none)
    struct ObjectCell {
      Entity entity = entt::null;
      glm::vec3 centre{0.0f};
      float radius = -1.0f;
      size_t orphan_edits = 0;
    };
    std::unordered_map<uint64_t, ObjectCell> placements;  // by cell key
    // Cells made from older draw settings (ProcModel's DrawVersion), each
    // kept until its cell is made again
    std::unordered_map<uint64_t, ObjectCell> old_placements;
    uint64_t draw_version = 0;
    Paging::CellJobs<RegionBuild> placing;
    // Hand edits on the cells' objects (the "objects" stage's), as loaded
    std::shared_ptr<const PCG::EditLayer> edits;
    // The objects' content with the objects the edits name after its own:
    // the cells' objects are made from it. The edits' objects by name, as
    // indices into it
    std::shared_ptr<const Worldgen::RegionContent> instance_content;
    std::shared_ptr<const std::unordered_map<std::string, int>> edit_objects;
    // The procmodels of its objects and creatures, loaded with the content
    RegionInstancer::Archetypes archetypes;
    // Those the object cells generate instances of: a kind with an imposter
    // places only its variants
    RegionInstancer::Archetypes object_archetypes;
    // Loaded regions whose objects are still being made, a few per frame
    struct ObjectWork {
      Entity region = entt::null;
      std::shared_ptr<const Worldgen::RegionContent> content;
      RegionBuild build;
      RegionInstancer::ObjectProgress progress;
    };
    std::deque<ObjectWork> making;
    // The planet's tile colours (sRGB); alpha 0: the look's colour
    std::array<glm::vec4, TerrainMaterialData::tile_count> tile_colours{};
    // The planet's seed and palettes, for the models' procedural textures
    Worldgen::TextureInput textures;
    // Its building styles and structures; null: no buildings
    std::shared_ptr<BuildingLibrary> buildings;
    // How its water's state changes; null: no water states
    std::shared_ptr<const Worldgen::WaterStateRules> water_states;
    // Its sea (a Water::Subsystem surface) and the waves of its states; null
    // entity: no sea
    std::shared_ptr<const Worldgen::WaterWaveSettings> water_waves;
    Entity sea = entt::null;
    // Its weather events: the schedule and kinds it was built with, the
    // events around the world clock, and the kind picked for the event that
    // warned at `picked_warning`
    Worldgen::WeatherEventSchedule weather_events = {.never = true};
    Worldgen::WeatherEventRules::KindList weather_event_kinds;
    uint64_t weather_seed = 0;
    std::vector<Worldgen::WeatherEvent> events;
    uint64_t picked_warning = UINT64_MAX;
    int picked_kind = -1;
    float picked_intensity = 0.0f;
  };

  // A craft volume's fleet: picked on a worker, then made a few craft per
  // frame within the frame budget (FrameBudget)
  // A worker's part of a craft build: the procmodels it read, by descriptor
  // path (null if one can't load), and each plan's parts
  struct CraftWork {
    std::unordered_map<std::string,
                       std::shared_ptr<ProcModel::ProcModelResource>>
        read;
    std::vector<std::optional<ProcModel::InstanceData>> instances;
    std::vector<std::optional<ProcModel::Subsystem::TextureSet>> textures;
  };
  struct CraftState {
    std::vector<CraftInstancer::CraftPlan> plans;
    std::future<CraftWork> generating;
    CraftWork work;
    size_t made = 0;
  };
  void Build(Entity volume, RegionGenerator& generator);
  void Build(Entity volume, CraftGenerator& generator);
  void Make(Entity volume, CraftGenerator& generator, CraftState& state);
  // `edits`: hand edits on what it places; null: none
  void Stream(Entity volume, PlanetGenerator& generator, PlanetState& state,
              bool reload, std::shared_ptr<const PCG::EditLayer> edits);
  // The weather event at `now` (seconds on the world clock)
  void Weather(PlanetGenerator& generator, PlanetState& state, double now);
  // The water's state at `now`, blending into a new one; the sea's waves
  // move towards it
  void Water(PlanetGenerator& generator, const PlanetState& state, double now);
  // Destroys the entities made from the volume, and their meshes
  void Clear(Entity volume);

  // A volume's terrain material, rebuilt when its look changes (the
  // volume's terrain then switches to the new one)
  struct TerrainLook {
    TerrainMaterialData data;
    MaterialHandle material = INVALID_MATERIAL;
    bgfx::TextureHandle tile_constants = BGFX_INVALID_HANDLE;
    TerrainTileImages images;  // reloaded only when the image paths change
  };
  MaterialHandle Look(Entity volume, const TerrainMaterialData& data);
  static void Release(TerrainLook& look);

  // The terrain mesh each region volume's terrain renders
  std::unordered_map<Entity, std::unique_ptr<Mesh>> meshes_;
  std::unordered_map<Entity, PlanetState> planets_;
  std::unordered_map<Entity, CraftState> crafts_;
  std::unordered_map<Entity, TerrainLook> looks_;
  std::filesystem::path root_;
};

}  // namespace ProcWorld

#endif  // PROCWORLD_H

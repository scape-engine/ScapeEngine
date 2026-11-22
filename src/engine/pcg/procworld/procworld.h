#ifndef PROCWORLD_H
#define PROCWORLD_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

#include "engine/ecs/world.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/material/terrain_material.h"
#include "engine/renderer/model/mesh.h"

#include "api/procedural_texture.h"
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

  // World position planets load their regions around (e.g. the camera)
  void SetViewer(const glm::vec3& position) { viewer_ = position; }

private:
  struct LoadedRegion {
    Entity entity = entt::null;
    std::unique_ptr<Mesh> terrain_mesh;
  };
  // A region built on a worker, with its buildings' pieces
  struct RegionBuild {
    Worldgen::Region region;
    RegionInstancer::BuildingPieces pieces;
  };
  struct PlanetState {
    // Shared with the jobs building from it, so a reload leaves them intact
    std::shared_ptr<const Worldgen::RegionContent> content;
    std::shared_ptr<const Worldgen::RegionContent> bare_content;  // no objects
    std::unordered_map<uint64_t, LoadedRegion> regions;  // by cell key
    std::unordered_map<uint64_t, std::future<RegionBuild>> building;
    // Herds come from regions of the herd level, apart from the terrain
    // (whose level changes under them): their content has no objects, the
    // terrain's no herds
    std::shared_ptr<const Worldgen::RegionContent> herd_content;
    std::unordered_map<uint64_t, Entity> herds;  // by cell key
    std::unordered_map<uint64_t, std::future<RegionBuild>> herd_building;
    // The planet's tile colours (sRGB); alpha 0: the look's colour
    std::array<glm::vec4, TerrainMaterialData::tile_count> tile_colours{};
    // The planet's seed and palettes, for the models' procedural textures
    Worldgen::TextureInput textures;
    // Its building styles and structures; null: no buildings
    std::shared_ptr<BuildingLibrary> buildings;
  };

  // A craft volume's fleet: picked on a worker thread, then made a few craft
  // per frame within the frame budget
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
  // Main-thread time a frame spends making a volume's entities (craft, a
  // planet's finished regions)
  static constexpr float frame_budget_ms = 2.0f;

  void Build(Entity volume, RegionGenerator& generator);
  void Build(Entity volume, CraftGenerator& generator);
  void Make(Entity volume, CraftGenerator& generator, CraftState& state);
  void Stream(Entity volume, PlanetGenerator& generator, PlanetState& state,
              bool reload);
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
  // Builds no longer wanted, kept until they finish (a future from
  // std::async blocks when destroyed)
  std::vector<std::future<RegionBuild>> discarded_;
  std::unordered_map<Entity, CraftState> crafts_;
  std::vector<std::future<CraftWork>> discarded_craft_;
  std::atomic<bool> stopping_{false};
  std::unordered_map<Entity, TerrainLook> looks_;
  std::filesystem::path root_;
  glm::vec3 viewer_{0.0f};
};

}  // namespace ProcWorld

#endif  // PROCWORLD_H

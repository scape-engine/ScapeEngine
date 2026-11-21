#ifndef PROCWORLD_H
#define PROCWORLD_H

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

namespace ProcWorld {

class RegionGenerator;
class PlanetGenerator;

// Builds worldgen terrain for the volumes whose generator is a
// RegionGenerator (one region) or a PlanetGenerator (regions around the
// viewer): terrain and objects become child entities of the volume entity
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
  struct PlanetState {
    // Shared with the jobs building from it, so a reload leaves them intact
    std::shared_ptr<const Worldgen::RegionContent> content;
    std::shared_ptr<const Worldgen::RegionContent> bare_content;  // no objects
    std::unordered_map<uint64_t, LoadedRegion> regions;  // by cell key
    std::unordered_map<uint64_t, std::future<Worldgen::Region>> building;
    // Herds come from regions of the herd level, apart from the terrain
    // (whose level changes under them): their content has no objects, the
    // terrain's no herds
    std::shared_ptr<const Worldgen::RegionContent> herd_content;
    std::unordered_map<uint64_t, Entity> herds;  // by cell key
    std::unordered_map<uint64_t, std::future<Worldgen::Region>> herd_building;
    // The planet's tile colours (sRGB); alpha 0: the look's colour
    std::array<glm::vec4, TerrainMaterialData::tile_count> tile_colours{};
    // The planet's seed and palettes, for the models' procedural textures
    Worldgen::TextureInput textures;
  };

  void Build(Entity volume, RegionGenerator& generator);
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
  std::vector<std::future<Worldgen::Region>> discarded_;
  std::unordered_map<Entity, TerrainLook> looks_;
  std::filesystem::path root_;
  glm::vec3 viewer_{0.0f};
};

}  // namespace ProcWorld

#endif  // PROCWORLD_H

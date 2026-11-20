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
#include "engine/renderer/model/mesh.h"

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
  };

  void Build(Entity volume, RegionGenerator& generator);
  void Stream(Entity volume, PlanetGenerator& generator, PlanetState& state,
              bool reload);
  // Destroys the entities made from the volume, and their meshes
  void Clear(Entity volume);
  MaterialHandle TerrainMaterial();

  // The terrain mesh each region volume's terrain renders
  std::unordered_map<Entity, std::unique_ptr<Mesh>> meshes_;
  std::unordered_map<Entity, PlanetState> planets_;
  // Builds no longer wanted, kept until they finish (a future from
  // std::async blocks when destroyed)
  std::vector<std::future<Worldgen::Region>> discarded_;
  MaterialHandle terrain_material_ = INVALID_MATERIAL;
  std::filesystem::path root_;
  glm::vec3 viewer_{0.0f};
};

}  // namespace ProcWorld

#endif  // PROCWORLD_H

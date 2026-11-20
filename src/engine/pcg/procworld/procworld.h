#ifndef PROCWORLD_H
#define PROCWORLD_H

#include <filesystem>
#include <memory>
#include <unordered_map>

#include "engine/ecs/world.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/mesh.h"

namespace ProcWorld {

class RegionGenerator;

// Builds worldgen regions for the volumes whose generator is a
// RegionGenerator: the terrain and objects become child entities of the
// volume entity
class Subsystem {
public:
  Subsystem() = default;
  ~Subsystem() = default;

  void Init();
  void Shutdown();

  // Rebuilds the dirty region volumes. Called from PCGEngine::ProcessQueued()
  void Update();

  // Content paths and object sources are relative to it
  void SetRoot(const std::filesystem::path& root) { root_ = root; }
  const std::filesystem::path& Root() const { return root_; }

private:
  void Build(Entity volume, RegionGenerator& generator);
  // Destroys the entities made from the volume, and their meshes
  void Clear(Entity volume);

  // The terrain mesh each volume's terrain renders
  std::unordered_map<Entity, std::unique_ptr<Mesh>> meshes_;
  MaterialHandle terrain_material_ = INVALID_MATERIAL;
  std::filesystem::path root_;
};

}  // namespace ProcWorld

#endif  // PROCWORLD_H

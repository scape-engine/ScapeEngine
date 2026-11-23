#ifndef WATER_SUBSYSTEM_H
#define WATER_SUBSYSTEM_H

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>

#include "engine/ecs/world.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/material/water_material.h"
#include "engine/renderer/model/mesh.h"
#include "water/wave_cascades.h"
#include "water/water_waves.h"

namespace Water {

// How a water surface is made: its wave cascades, the grid drawn under the
// viewer and its look. Edits apply when the surface is made again
struct SurfaceSettings {
  Worldgen::WaveCascadeSettings cascades;
  float patch_size = 4096.0f;  // metres across the grid
  int grid_resolution = 256;   // vertices across, closer in the middle
  WaterMaterialData look;
};

// Water surfaces (WaveFieldComponent): moves their waves on every frame and
// turns them into the generic components the renderer reads, a compute job
// writing the wave cascades and the shader params the water material reads.
// Nothing here is tied to planets
class Subsystem {
public:
  void SetViewer(const glm::vec3& position) { viewer_ = position; }

  // Makes `entity` a water surface of `waves` (shared, not copied) on its
  // parent's sphere of `radius` (0: the parent's y = 0 plane)
  void Make(ECS& ecs, Entity entity,
            std::shared_ptr<const Worldgen::WaterWaveSettings> waves,
            float radius, const SurfaceSettings& settings);

  // Every surface, once per frame before it is drawn
  void Update(ECS& ecs);

  void Shutdown();

private:
  // A surface's grid and material, released once its entity is gone
  struct Surface {
    std::unique_ptr<Mesh> mesh;
    MaterialHandle material = INVALID_MATERIAL;
  };
  static void Release(Surface& surface);

  std::unordered_map<Entity, Surface> surfaces_;
  glm::vec3 viewer_{0.0f};
};

}  // namespace Water

#endif  // WATER_SUBSYSTEM_H

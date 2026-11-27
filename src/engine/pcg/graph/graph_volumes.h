#ifndef GRAPH_VOLUMES_H
#define GRAPH_VOLUMES_H

#include <future>
#include <memory>
#include <unordered_map>

#include "engine/core/types/geometry_data.h"
#include "engine/ecs/world.h"
#include "engine/renderer/model/mesh.h"

namespace PCG {

class GeneratorGraph;

// Graph generators' volumes (VolumeComponent): each one's graph evaluated
// into a heightfield over its bounds, Input X and Input Y its x and z, at
// its resolution, on the workers; shown as a generated "Terrain" child once
// made, the old one kept until then. As godot_voxel's VoxelTerrain with its
// heightmap mesher, in one piece
// TODO: blocks, LODs and streaming around the viewer (godot_voxel's
// terrain/ and streams/); a material from the graph's outputs
class GraphVolumes {
public:
  // Remakes the dirty volumes' terrain, shows what is made
  void Update(ECS& ecs);

  void Shutdown();

  // `graph` sampled resolution x resolution times over min..max (x and z),
  // its heights up y; empty when it doesn't compile
  static Geometry::MeshData Heightfield(const GeneratorGraph& graph,
                                        glm::vec2 min, glm::vec2 max,
                                        uint16_t resolution);

private:
  struct Terrain {
    std::future<Geometry::MeshData> making;
    std::unique_ptr<Mesh> mesh;
    Entity entity = entt::null;  // the volume's "Terrain" child
  };
  static void Release(ECS& ecs, Terrain& terrain);

  std::unordered_map<Entity, Terrain> terrains_;
};

}  // namespace PCG

#endif  // GRAPH_VOLUMES_H

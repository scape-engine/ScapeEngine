#ifndef REGION_INSTANCER_H
#define REGION_INSTANCER_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/ecs/world.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/mesh.h"
#include "engine/pcg/procworld/instancing/building_library.h"

#include "api/procedural_texture.h"
#include "api/region.h"

namespace ProcWorld {

// Makes a built region's entities: its terrain and one entity per object
// instance, as children of the source (the volume entity, which marks the
// region origin), tagged with ProceduralInstanceComponent.
// Object ids are sources: "primitive:<shape>", "procmodel:<descriptor>" or a
// model path, relative to the project root. A skirt depth hangs a strip
// from the terrain's border (see RegionMesher). A planet (its seed and
// palettes) colours the procmodels' procedural textures
class RegionInstancer {
public:
  struct InstanceResult {
    Entity terrain = entt::null;
    std::vector<Entity> object_entities;
    std::unique_ptr<Mesh> terrain_mesh;  // owned by the caller while in use
  };

  static InstanceResult Instance(ECS& ecs, const Worldgen::Region& region,
                                 const Worldgen::RegionContent& content,
                                 Entity source, MaterialHandle terrain_material,
                                 const std::filesystem::path& root,
                                 float skirt_depth = 0.0f,
                                 const Worldgen::TextureInput* planet = nullptr);

  // One entity per creature of the region's herds, as children of the source,
  // made from the content's herd ids (sources as above)
  static std::vector<Entity> InstanceHerds(
      ECS& ecs, const Worldgen::Region& region,
      const Worldgen::RegionContent& content, Entity source,
      const std::filesystem::path& root,
      const Worldgen::TextureInput* planet = nullptr);

  // A region's building pieces by source, as matrices in the region
  struct BuildingPieces {
    std::map<std::string, std::vector<glm::mat4>> models;  // and primitives
    struct Object {
      std::string id;
      uint64_t seed;
      glm::mat4 matrix;
    };
    std::vector<Object> objects;  // procmodels: their seeds pick their parts
    size_t buildings = 0;
  };

  // The region's buildings' pieces (sources as above). Any thread: it also
  // reads their models' data, so instancing them only uploads
  static BuildingPieces GatherBuildings(const Worldgen::Region& region,
                                        BuildingLibrary& library,
                                        const std::filesystem::path& root);

  // The pieces as children of the source: one entity whose
  // InstancedMeshComponent draws every model piece, a part per model mesh; one
  // entity per procmodel piece
  // TODO: instance procmodel pieces too
  static std::vector<Entity> InstanceBuildings(
      ECS& ecs, const BuildingPieces& pieces, Entity source,
      const std::filesystem::path& root,
      const Worldgen::TextureInput* planet = nullptr);

  // One instance of a source (as above, the seed picks a procmodel's parts),
  // a child of the source entity; entt::null if it can't be made
  static Entity CreateObject(ECS& ecs, const std::string& id, Entity source,
                             uint64_t seed, const std::filesystem::path& root,
                             const Worldgen::TextureInput* planet = nullptr);
};

}  // namespace ProcWorld

#endif  // REGION_INSTANCER_H

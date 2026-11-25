#ifndef REGION_INSTANCER_H
#define REGION_INSTANCER_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ecs/world.h"
#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/mesh.h"
#include "engine/pcg/procmodel/procmodel.h"
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
  // Loaded procmodels by object id ("procmodel:<descriptor>"), for
  // GenerateObjects
  using Archetypes =
      std::unordered_map<std::string,
                         std::shared_ptr<const ProcModel::ProcModelResource>>;
  // Procmodel instances generated off the main thread, by object id and seed;
  // nullopt: generation failed. Objects not here are generated when made
  using ObjectModels =
      std::map<std::pair<std::string, uint64_t>,
               std::optional<ProcModel::InstanceData>>;

  // The procmodel instances the region's objects and herds (with `herds`)
  // will need, from the archetypes given. Any thread
  static ObjectModels GenerateObjects(const Worldgen::Region& region,
                                      const Worldgen::RegionContent& content,
                                      bool herds, const Archetypes& archetypes,
                                      const ProcModel::Subsystem& procmodel,
                                      const Worldgen::TextureInput* planet);

  struct InstanceResult {
    Entity terrain = entt::null;
    size_t objects = 0;
    std::unique_ptr<Mesh> terrain_mesh;  // owned by the caller while in use
  };

  static InstanceResult Instance(ECS& ecs, const Worldgen::Region& region,
                                 const Worldgen::RegionContent& content,
                                 Entity source, MaterialHandle terrain_material,
                                 const std::filesystem::path& root,
                                 float skirt_depth = 0.0f,
                                 const Worldgen::TextureInput* planet = nullptr,
                                 const ObjectModels* models = nullptr,
                                 bool objects = true);

  // How far InstanceObjects got through a region's objects
  struct ObjectProgress {
    size_t kind = 0;  // into region.objects
    size_t at = 0;    // into its matrices
    uint64_t seed = 0;
    bool started = false;
    size_t made = 0;  // objects, instanced or not
    // The instanced ones' matrices in the region and their objects' origins
    // (InstancedMeshComponent::anchors_), by mesh, material, their kind's
    // fade distances and their draw range (InstancedMeshComponent::Part)
    struct PartInstances {
      std::vector<glm::mat4> matrices;
      std::vector<glm::vec3> anchors;
    };
    std::map<std::tuple<const Mesh*, MaterialHandle, float, float, float,
                        float, float>,
             PartInstances>
        instances;
    // What keeps the instanced ones' meshes alive (deformed ones,
    // imposters), for the set
    std::vector<std::shared_ptr<const void>> owned;
    // The current kind's imposter, once looked up (null: none)
    std::shared_ptr<const ProcModel::Subsystem::ModelImposter> imposter;
    size_t imposter_kind = SIZE_MAX;
  };
  // The region's objects (as Instance makes them) from where `progress`
  // stands, one at a time while `more` allows, so a frame can stop partway;
  // true once every one is made. Static models are drawn as instances, one
  // InstancedMeshComponent for the region made once all are gathered;
  // animated ones and primitives get an entity each. `made` collects the
  // entities, the instanced set's among them.
  // Instanced parts take their kind's fade distances (ObjectSpawn). A
  // procmodel with an imposter places only the imposter's variants (picked by
  // the object's seed), so its card shows the same model; once baked, the
  // card draws past the imposter's distance.
  // TODO: Coarser meshes at the kind's LOD distances
  // TODO: Other engine runtime systems. The game's own streaming, LOD
  // selection and render batching can be decompiled like the PCG code was;
  // a larger project of its own
  static bool InstanceObjects(ECS& ecs, const Worldgen::Region& region,
                              const Worldgen::RegionContent& content,
                              Entity source, const std::filesystem::path& root,
                              const Worldgen::TextureInput* planet,
                              const ObjectModels* models,
                              ObjectProgress& progress,
                              const std::function<bool()>& more,
                              std::vector<Entity>* made = nullptr);

  // One entity per creature of the region's herds, as children of the source,
  // made from the content's herd ids (sources as above)
  static std::vector<Entity> InstanceHerds(
      ECS& ecs, const Worldgen::Region& region,
      const Worldgen::RegionContent& content, Entity source,
      const std::filesystem::path& root,
      const Worldgen::TextureInput* planet = nullptr,
      const ObjectModels* models = nullptr);

  // A region's building pieces by source, as matrices in the region with
  // their buildings' origins as anchors, so a building fades and switches LOD
  // as a whole
  struct BuildingPieces {
    // By model, primitives too
    std::map<std::string, ObjectProgress::PartInstances> models;
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
                             const Worldgen::TextureInput* planet = nullptr,
                             const ObjectModels* models = nullptr);
};

}  // namespace ProcWorld

#endif  // REGION_INSTANCER_H

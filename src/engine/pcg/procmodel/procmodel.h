#ifndef PROCMODEL_H
#define PROCMODEL_H

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_set>

#include "engine/memory/resource.h"

#include "engine/pcg/pipeline/operation_registry.h"

#include "engine/pcg/procmodel/descriptor/model_descriptor.h"
#include "engine/pcg/procmodel/generator/operations.h"
#include "engine/pcg/procmodel/generator/resolved_model.h"
#include "engine/pcg/procmodel/instantiator/model_instantiator.h"
#include "engine/pcg/procmodel/procmodel_resource.h"
#include "engine/pcg/procmodel/validation/validation_logger.h"
#include "engine/renderer/imposter/imposter.h"
#include "engine/renderer/model/draw_settings.h"

#include "api/procedural_texture.h"

namespace ProcModel {

class Subsystem {
public:
  Subsystem();
  ~Subsystem() = default;

  void Init();
  void Shutdown();
  void ResetSamples();

  std::optional<ProcModel::InstanceData> GenerateInstance(
      const std::string& descriptor_path, uint64_t seed);

  // `planet`: the seed and palettes of the planet the model grows on; the
  // descriptor's procedural textures take its colours. Without it (or its
  // palettes) the model keeps its authored materials
  ModelInstantiator::InstantiateResult SpawnInstance(
      const std::string& descriptor_path, uint64_t seed,
      Entity parent = entt::null,
      const Worldgen::TextureInput* planet = nullptr);
  // The same with the instance generated beforehand (Generate, e.g. on a
  // worker) from the descriptor's archetype; keeps no validation samples
  ModelInstantiator::InstantiateResult SpawnInstance(
      const std::string& descriptor_path,
      const ProcModel::InstanceData& instance, Entity parent = entt::null,
      const Worldgen::TextureInput* planet = nullptr);

  // An instance's deformed geometry (InstanceData::instance_geometry)
  // uploaded, standing in for the graph's meshes; each has LODs made from it
  // as ApplyDraw makes the graph's (LOD models stay as authored)
  struct DeformedMeshes {
    std::vector<std::unique_ptr<Mesh>> owned;  // and their LODs
    ModelInstantiator::MeshOverrides meshes;
    // Merged instead: all the instance's parts in its space (before
    // model_scale), one mesh per material, so it draws in a call per material
    std::vector<const Mesh*> merged;
  };
  // The instance's deformed meshes, made once per archetype, seed and
  // `merge` while anything holds them (main thread); null when it deforms
  // nothing. A merge falls back to `meshes` when a mesh is skinned or a LOD
  // is an authored model
  std::shared_ptr<const DeformedMeshes> Deformed(
      const ProcModelResource& archetype,
      const ProcModel::InstanceData& instance, bool merge = false);

  // Its parts instead of entities (ModelInstantiator::Parts), for instancing.
  // With `deformed` they draw the instance's deformed meshes, merged by
  // material where they can be, which it keeps alive; without, the graph's
  std::optional<std::vector<ModelInstantiator::Part>> InstanceParts(
      const std::string& descriptor_path,
      const ProcModel::InstanceData& instance,
      const Worldgen::TextureInput* planet = nullptr,
      std::shared_ptr<const DeformedMeshes>* deformed = nullptr);

  // A spawn in two steps, so the picking can leave the main thread. Archetype
  // loads the descriptor's model on first use (main thread; null if it
  // can't); Generate picks and places the parts of `seed` from a loaded one
  // on any thread, keeping no validation samples; Instantiate makes the
  // entities (main thread)
  std::shared_ptr<const ProcModelResource> Archetype(
      const std::string& descriptor_path);
  // Archetype's load in two steps, so reading the model can leave the main
  // thread: ReadArchetype reads and resolves it (any thread; null if it
  // can't), AddArchetype uploads its meshes and keeps it (main thread; the
  // one kept if one is). LoadedArchetype: the kept one, or null
  std::shared_ptr<ProcModelResource> ReadArchetype(
      const std::string& descriptor_path) const;
  std::shared_ptr<const ProcModelResource> AddArchetype(
      const std::string& descriptor_path,
      std::shared_ptr<ProcModelResource> read);
  std::shared_ptr<const ProcModelResource> LoadedArchetype(
      const std::string& descriptor_path) const;
  std::optional<ProcModel::InstanceData> Generate(
      const ProcModelResource& archetype, uint64_t seed) const;
  // `materials`: the graph's materials to use instead (ApplyTextures'). The
  // root keeps the instance's deformed meshes (MeshOwnerComponent)
  ModelInstantiator::InstantiateResult Instantiate(
      const ProcModel::InstanceData& instance, const ProcModelResource& archetype,
      Entity parent = entt::null,
      const std::vector<MaterialHandle>* materials = nullptr);

  // A model's procedural textures for one texture input, in two steps so the
  // compositing can leave the main thread: PrepareTextures composites each
  // textured material's images (any thread), at most `max_size` a side (0:
  // as composited); ApplyTextures stores them and gives the graph's
  // materials with them (main thread), one list per descriptor and input.
  // With `claims`, an image another set claimed first is left to it: store
  // every set's images (StoreTextureImages) before applying any
  struct TextureClaims {
    // True for the first claim of `key` (any thread)
    bool Claim(const std::string& key) {
      std::lock_guard lock(mutex);
      return keys.insert(key).second;
    }
    std::mutex mutex;
    std::unordered_set<std::string> keys;
  };
  struct TextureSet {
    struct Image {
      std::string key;  // empty: none
      Worldgen::TextureImage image;
    };
    struct Material {
      std::string name;  // the graph material's
      Image albedo, normal, masks, occlusion;
    };
    std::string key;
    std::vector<Material> materials;
  };
  TextureSet PrepareTextures(const std::string& descriptor_path,
                             const ModelDescriptor& descriptor,
                             const Worldgen::TextureInput& input,
                             int max_size = 0,
                             TextureClaims* claims = nullptr);
  void StoreTextureImages(TextureSet& set);
  const std::vector<MaterialHandle>* ApplyTextures(
      const std::string& descriptor_path, const ProcModelResource& archetype,
      TextureSet set);

  PCG::OperationRegistry& GetOperationRegistry() { return operation_registry_; }
  const PCG::OperationRegistry& GetOperationRegistry() const {
    return operation_registry_;
  }

  ValidationLogger& ValidationLog();

  ResourceID LoadArchetype(const std::string& descriptor_path);

  // A model's imposter (its draw settings' imposter) and where its cards take
  // over from the meshes
  struct ModelImposter {
    std::unique_ptr<::Imposter> imposter;
    float distance = 0.0f;
    float blend = 0.0f;  // the fraction of distance they cross-fade over
    // The variants' deformed meshes, which the variants' parts draw
    std::vector<std::shared_ptr<const DeformedMeshes>> deformed;
  };
  // The model's imposter for the planet, made on first ask (main thread) from
  // the first draw-settings variants seeds; null when its draw settings have
  // none (distance 0) or it can't load. It draws once baked
  const ModelImposter* Imposter(const std::string& descriptor_path,
                                const Worldgen::TextureInput* planet);

  // The project root, which the models' LOD sources are relative to
  void SetRoot(const std::filesystem::path& root) { root_ = root; }
  // Applies a source model's draw settings again to the loaded archetypes
  // made from it (after they were edited)
  void ReloadDrawSettings(const std::filesystem::path& model);

private:
  // The source model's draw settings (DrawSettings) on the graph's meshes:
  // mesh i of each LOD model stands in for mesh i; LODs without a model are
  // made from the graph's meshes
  void ApplyDraw(const ModelGraph& graph);

  // The model's materials with its procedural textures made for the planet,
  // by the graph's material index; null when it has none to make
  const std::vector<MaterialHandle>* TexturedMaterials(
      const std::string& descriptor_path, const ProcModelResource& archetype,
      const Worldgen::TextureInput& planet);
  // Any thread
  const Worldgen::TextureContent* LoadTextureContent(const std::string& path);
  // A texture layer's image at most `max_size` a side (0: as it is), or
  // null if it can't load (any thread)
  const Worldgen::TextureImage* LoadLayerImage(const std::string& path,
                                               int max_size);

  PCG::OperationRegistry operation_registry_;
  std::unique_ptr<ValidationLogger> validation_logger_;
  std::unordered_map<std::string, ResourceID> procmodel_cache_;
  std::filesystem::path root_;
  // Generated LOD meshes, by source model and detail (ApplyDraw)
  std::unordered_map<std::string, std::vector<std::unique_ptr<Mesh>>>
      generated_lods_;
  // The draw settings ApplyDraw gave each source model, for Deformed
  std::unordered_map<std::string, DrawSettings> draw_settings_;
  // Deformed's, by archetype, seed and merge; swept of expired ones as it
  // grows
  std::map<std::tuple<const ProcModelResource*, uint64_t, bool>,
           std::weak_ptr<const DeformedMeshes>>
      deformed_;
  size_t deformed_sweep_ = 64;

  // By path; unset if it failed to load. Guarded by texture_mutex_ (the
  // values stay put: readers keep pointers to them)
  std::mutex texture_mutex_;
  std::unordered_map<std::string, std::optional<Worldgen::TextureContent>>
      texture_contents_;
  std::unordered_map<std::string, Worldgen::TextureImage> layer_images_;
  // By descriptor, material and texture key
  std::unordered_map<std::string, MaterialHandle> textured_materials_;
  // By descriptor and planet
  std::unordered_map<std::string, std::vector<MaterialHandle>>
      model_materials_;

  // By descriptor and planet (Imposter); kept while anything may draw them
  std::unordered_map<std::string, ModelImposter> imposters_;

  std::vector<ProcModelSample> samples_;
};

}  // namespace ProcModel

#endif  // PROCMODEL_H
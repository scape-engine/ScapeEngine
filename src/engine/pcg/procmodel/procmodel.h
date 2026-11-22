#ifndef PROCMODEL_H
#define PROCMODEL_H

#include <memory>

#include "engine/memory/resource.h"

#include "engine/pcg/pipeline/operation_registry.h"

#include "engine/pcg/procmodel/descriptor/model_descriptor.h"
#include "engine/pcg/procmodel/generator/operations.h"
#include "engine/pcg/procmodel/generator/resolved_model.h"
#include "engine/pcg/procmodel/instantiator/model_instantiator.h"
#include "engine/pcg/procmodel/procmodel_resource.h"
#include "engine/pcg/procmodel/validation/validation_logger.h"

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
  // TODO: Add logic to instantiate deformation (missing)
  ModelInstantiator::InstantiateResult SpawnInstance(
      const std::string& descriptor_path, uint64_t seed,
      Entity parent = entt::null,
      const Worldgen::TextureInput* planet = nullptr);

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
  ModelInstantiator::InstantiateResult Instantiate(
      const std::string& descriptor_path, const ProcModelResource& archetype,
      const ProcModel::InstanceData& instance, Entity parent = entt::null,
      const Worldgen::TextureInput* planet = nullptr);

  PCG::OperationRegistry& GetOperationRegistry() { return operation_registry_; }
  const PCG::OperationRegistry& GetOperationRegistry() const {
    return operation_registry_;
  }

  ValidationLogger& ValidationLog();

  ResourceID LoadArchetype(const std::string& descriptor_path);

private:
  // The model's materials with its procedural textures made for the planet,
  // by the graph's material index; null when it has none to make
  const std::vector<MaterialHandle>* TexturedMaterials(
      const std::string& descriptor_path, const ModelDescriptor& descriptor,
      const ModelGraph& graph, const Worldgen::TextureInput& planet);
  const Worldgen::TextureContent* LoadTextureContent(const std::string& path);
  // A texture layer's image, or null if it can't load
  const Worldgen::TextureImage* LoadLayerImage(const std::string& path);

  PCG::OperationRegistry operation_registry_;
  std::unique_ptr<ValidationLogger> validation_logger_;
  std::unordered_map<std::string, ResourceID> procmodel_cache_;

  // By path; unset if it failed to load
  std::unordered_map<std::string, std::optional<Worldgen::TextureContent>>
      texture_contents_;
  std::unordered_map<std::string, Worldgen::TextureImage> layer_images_;
  // By descriptor, material and texture key
  std::unordered_map<std::string, MaterialHandle> textured_materials_;
  // By descriptor and planet
  std::unordered_map<std::string, std::vector<MaterialHandle>>
      model_materials_;

  std::vector<ProcModelSample> samples_;
};

}  // namespace ProcModel

#endif  // PROCMODEL_H
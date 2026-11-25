#ifndef PROCMODEL_H
#define PROCMODEL_H

#include "engine/memory/resource.h"

#include "engine/pcg/pipeline/operation_registry.h"

#include "engine/pcg/procmodel/generator/operations.h"
#include "engine/pcg/procmodel/generator/resolved_model.h"
#include "engine/pcg/procmodel/instantiator/model_instantiator.h"
#include "engine/pcg/procmodel/validation/validation_logger.h"

namespace ProcModel {

class Subsystem {
public:
  Subsystem();
  ~Subsystem() = default;

  void Init();
  void Shutdown();
  void ResetSamples();

  // Instance of a loaded archetype (see LoadArchetype), nullopt while it
  // loads or if generation fails
  std::optional<ProcModel::InstanceData> GenerateInstance(ResourceID archetype,
                                                          uint64_t seed);

  // TODO: Add logic to instantiate deformation (missing)
  ModelInstantiator::InstantiateResult SpawnInstance(
      const std::string& descriptor_path, uint64_t seed,
      Entity parent = entt::null);

  PCG::OperationRegistry& GetOperationRegistry() { return operation_registry_; }
  const PCG::OperationRegistry& GetOperationRegistry() const {
    return operation_registry_;
  }

  ValidationLogger& ValidationLog();

  // Starts loading the archetype in the background (cached by path) and
  // returns its resource right away. Usable once the ProcModelResource
  // IsResolved(); a failed load is retried on the next call
  ResourceID LoadArchetype(const std::string& descriptor_path);

  // Loads the descriptor again, e.g. after it was edited and saved
  ResourceID ReloadArchetype(const std::string& descriptor_path);

private:
  PCG::OperationRegistry operation_registry_;
  std::unique_ptr<ValidationLogger> validation_logger_;
  std::unordered_map<std::string, ResourceID> procmodel_cache_;

  std::vector<ProcModelSample> samples_;
};

}  // namespace ProcModel

#endif  // PROCMODEL_H
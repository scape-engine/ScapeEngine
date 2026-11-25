#ifndef MODEL_GENERATOR_H
#define MODEL_GENERATOR_H

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/pcg/pipeline/linear_pipeline.h"

#include "engine/pcg/procmodel/descriptor/model_descriptor.h"

#include "engine/pcg/procmodel/generator/locator_context.h"
#include "engine/pcg/procmodel/generator/model_context.h"
#include "engine/pcg/procmodel/generator/resolved_model.h"

#include "engine/pcg/procmodel/model_graph/model_graph.h"

#include "engine/pcg/procmodel/validation/procmodel_validator.h"

namespace ProcModel {

class ModelGenerator {
public:
  static std::optional<InstanceData> Generate(
      const ModelGraph& graph, const ModelDescriptor& descriptor,
      const PCG::LinearPipeline& pipeline,
      const PCG::OperationRegistry& operation_registry, uint64_t seed,
      int max_retries = 10,
      std::vector<ProcModelSample>* out_samples = nullptr);

  // The part's transform in model space, with its sampled rotation and scale,
  // composed as ModelInstantiator composes it
  static glm::mat4 PartWorld(const ResolvedDescriptor& d);

private:
  static void ApplyParameterBindings(
      std::vector<ResolvedDescriptor>& descriptors,
      const std::vector<ParameterBinding>& bindings);

  static bool ValidateConstraints(
      const std::unordered_set<std::string>& selected_ids,
      const std::vector<ConstraintRule>& constraints);
};

}  // namespace ProcModel

#endif

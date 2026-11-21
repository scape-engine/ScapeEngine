#ifndef MODEL_INSTANTIATOR_H
#define MODEL_INSTANTIATOR_H

#include <vector>

#include "engine/ecs/world.h"

#include "engine/pcg/procmodel/generator/resolved_model.h"
#include "engine/pcg/procmodel/model_graph/model_graph.h"

namespace ProcModel {

class ModelInstantiator {
public:
  struct InstantiateResult {
    Entity root = entt::null;
    std::vector<Entity> part_entities;
  };

  // `materials`, by the graph's material index, replace the graph's own
  static InstantiateResult Instantiate(
      const InstanceModel& resolved, const ModelGraph& graph,
      Entity parent = entt::null,
      const std::vector<MaterialHandle>* materials = nullptr);

private:
  static Entity CreatePartEntity(const ResolvedDescriptor& descriptor,
                                 const ModelGraph& graph, Entity parent,
                                 const std::vector<MaterialHandle>& materials);
};

}  // namespace ProcModel

#endif  // MODEL_INSTANTIATOR_H

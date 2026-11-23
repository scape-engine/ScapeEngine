#ifndef MODEL_INSTANTIATOR_H
#define MODEL_INSTANTIATOR_H

#include <optional>
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
  // A model's meshes with their materials and matrices in its space (scaled
  // as a whole), as Instantiate places them, for instancing; nullopt when a
  // mesh is skinned (those animate, so they need Instantiate's entities)
  struct Part {
    const Mesh* mesh = nullptr;
    MaterialHandle material = INVALID_MATERIAL;
    glm::mat4 matrix{1.0f};
  };
  static std::optional<std::vector<Part>> Parts(
      const InstanceModel& resolved, const ModelGraph& graph,
      const std::vector<MaterialHandle>* materials = nullptr);

  static InstantiateResult Instantiate(
      const InstanceModel& resolved, const ModelGraph& graph,
      Entity parent = entt::null,
      const std::vector<MaterialHandle>* materials = nullptr);

private:
  static Entity CreatePartEntity(const ResolvedDescriptor& descriptor,
                                 const ModelGraph& graph, Entity parent,
                                 const std::vector<MaterialHandle>& materials,
                                 Entity animator);
};

}  // namespace ProcModel

#endif  // MODEL_INSTANTIATOR_H

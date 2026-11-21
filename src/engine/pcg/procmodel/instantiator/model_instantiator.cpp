#include "model_instantiator.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "engine/ecs/ecs_collection.h"
#include "engine/ecs/world.h"
#include "engine/math/transformation.h"
#include "engine/renderer/model/mesh.h"

namespace ProcModel {

ModelInstantiator::InstantiateResult ModelInstantiator::Instantiate(
    const InstanceModel& resolved, const ModelGraph& graph, Entity parent,
    const std::vector<MaterialHandle>* materials) {
  InstantiateResult result;
  auto& ecs = ECS::Main();

  // Create root entity for this resolved model
  if (parent != entt::null) {
    auto [root, transform] = ecs.CreateEntity(resolved.model_id, parent);
    result.root = root;

    // Apply whole-model scale
    transform.scale_ = resolved.model_scale;
    transform.modified_ = true;
  } else {
    auto [root, transform] = ecs.CreateEntity(resolved.model_id);
    result.root = root;

    transform.scale_ = resolved.model_scale;
    transform.modified_ = true;
  }

  // Part transforms are in model space (as the previews read them), so every
  // part sits directly under the root
  for (const auto& descriptor : resolved.descriptors) {
    result.part_entities.push_back(
        CreatePartEntity(descriptor, graph, result.root,
                         materials ? *materials : graph.material_handles));
  }

  return result;
}

Entity ModelInstantiator::CreatePartEntity(const ResolvedDescriptor& descriptor,
                                           const ModelGraph& graph,
                                           Entity parent,
                                           const std::vector<MaterialHandle>& materials) {
  auto& ecs = ECS::Main();
  const auto material = [&](const Mesh& mesh) {
    const uint32_t index = mesh.MaterialIndex();
    return index < materials.size() ? materials[index] : INVALID_MATERIAL;
  };

  auto [entity, transform] = ecs.CreateEntity(descriptor.descriptor_id, parent);

  // Decompose the base local transform from graph node
  glm::vec3 position;
  glm::quat rotation;
  glm::vec3 scale;
  glm::vec3 skew;
  glm::vec4 perspective;
  glm::decompose(descriptor.local_transform, scale, rotation, position, skew,
                 perspective);

  transform.position_ = position;
  transform.rotation_ = rotation;
  transform.scale_ = scale;

  // Apply sampled rotation on top of base rotation
  glm::quat applied_rot = glm::quat(descriptor.applied_rotation);
  transform.rotation_ = glm::normalize(transform.rotation_ * applied_rot);
  transform.scale_ *= descriptor.applied_scale;

  transform.euler_angles_ = glm::degrees(glm::eulerAngles(transform.rotation_));
  transform.modified_ = true;

  // Attach mesh renderers
  if (descriptor.mesh_indices.size() == 1) {
    // Single mesh — attach directly to part entity
    const Mesh* mesh = graph.GetMesh(descriptor.mesh_indices[0]);
    if (mesh) {
      auto& renderer = ecs.Add<MeshRendererComponent>(entity);
      renderer.mesh_ = mesh;
      renderer.enabled_ = true;
      renderer.material_ = material(*mesh);
    }
  } else if (descriptor.mesh_indices.size() > 1) {
    // Multiple meshes — create child entity per mesh
    for (size_t i = 0; i < descriptor.mesh_indices.size(); ++i) {
      const Mesh* mesh = graph.GetMesh(descriptor.mesh_indices[i]);
      if (!mesh)
        continue;

      std::string child_name =
          descriptor.descriptor_id + "_mesh_" + std::to_string(i);
      auto [child, child_transform] = ecs.CreateEntity(child_name, entity);

      // Identity transform — inherit parent's transform through hierarchy
      child_transform.modified_ = true;

      auto& renderer = ecs.Add<MeshRendererComponent>(child);
      renderer.mesh_ = mesh;
      renderer.enabled_ = true;
      renderer.material_ = material(*mesh);
    }
  }

  return entity;
}
}  // namespace ProcModel

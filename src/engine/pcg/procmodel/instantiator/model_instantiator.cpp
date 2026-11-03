#include "model_instantiator.h"

#include <unordered_map>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "engine/ecs/ecs_collection.h"
#include "engine/ecs/world.h"
#include "engine/math/transformation.h"
#include "engine/renderer/model/mesh.h"

namespace ProcModel {

ModelInstantiator::InstantiateResult ModelInstantiator::Instantiate(
    const InstanceModel& resolved, const ModelGraph& graph, Entity parent) {
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

  std::unordered_map<std::string, Entity> entity_by_descriptor_id;

  for (const auto& descriptor : resolved.descriptors) {
    Entity parent_entity = result.root;

    if (!descriptor.activator_id.empty()) {
      auto it = entity_by_descriptor_id.find(descriptor.activator_id);
      if (it != entity_by_descriptor_id.end()) {
        parent_entity = it->second;
      }
      // If activator isn't found, fall back to root.
    }

    Entity entity = CreatePartEntity(descriptor, graph, parent_entity);
    entity_by_descriptor_id[descriptor.descriptor_id] = entity;
    result.part_entities.push_back(entity);
  }

  return result;
}

Entity ModelInstantiator::CreatePartEntity(const ResolvedDescriptor& descriptor,
                                           const ModelGraph& graph,
                                           Entity parent) {
  auto& ecs = ECS::Main();

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

  // Get materials from graph for registration
  const auto& materials = graph.GetMaterials();

  // Attach mesh renderers
  if (descriptor.mesh_indices.size() == 1) {
    // Single mesh — attach directly to part entity
    const Mesh* mesh = graph.GetMesh(descriptor.mesh_indices[0]);
    if (mesh) {
      auto& renderer = ecs.Add<MeshRendererComponent>(entity);
      renderer.mesh_ = mesh;
      renderer.enabled_ = true;

      // Register material and assign handle
      uint32_t mat_idx = mesh->MaterialIndex();
      if (mat_idx < materials.size()) {
        renderer.material_ =
            MaterialRegistry::Instance().Register(materials[mat_idx]);
      } else {
        renderer.material_ = INVALID_MATERIAL;
      }
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

      // Register material and assign handle
      uint32_t mat_idx = mesh->MaterialIndex();
      if (mat_idx < materials.size()) {
        renderer.material_ =
            MaterialRegistry::Instance().Register(materials[mat_idx]);
      } else {
        renderer.material_ = INVALID_MATERIAL;
      }
    }
  }

  return entity;
}
}  // namespace ProcModel

#include "model_instantiator.h"

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "engine/ecs/ecs_collection.h"
#include "engine/ecs/world.h"
#include "engine/math/transformation.h"
#include "engine/animation/animation.h"
#include "engine/ecs/components/animator_component.h"
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

  // A model with skinned meshes plays its first clip on its root, started at
  // a point from the entity so herds move out of step, shaped by its seeded
  // poses
  Entity animator = entt::null;
  if (std::any_of(graph.gpu_meshes.begin(), graph.gpu_meshes.end(),
                  [](const auto& mesh) { return mesh->Skinned(); })) {
    if (AnimatorComponent* added =
            Animation::AddAnimator(ecs, result.root, &graph.skeletal)) {
      if (!graph.skeletal.clips.empty())
        added->time_ =
            graph.skeletal.clips[0].duration *
            std::fmod(static_cast<float>(static_cast<uint32_t>(result.root)) *
                          0.618034f,
                      1.0f);
      // Each creature draws its own poses, as each of a species differs
      if (graph.poses) {
        added->poses_ = PoseLayers(
            *graph.poses, graph.skeletal, graph.pose_clip,
            resolved.seed ^ (static_cast<uint64_t>(result.root) + 1) *
                                 0x9e3779b97f4a7c15ull,
            resolved.model_scale.x);
        Animation::Resample(*added);
      }
      added->gaits_ = graph.gaits;
      animator = result.root;
    }
  }

  // Part transforms are in model space (as the previews read them), so every
  // part sits directly under the root
  for (const auto& descriptor : resolved.descriptors) {
    result.part_entities.push_back(
        CreatePartEntity(descriptor, graph, result.root,
                         materials ? *materials : graph.material_handles,
                         animator));
  }

  return result;
}

// A part's matrix as CreatePartEntity sets its transform
static glm::mat4 _PartMatrix(const ResolvedDescriptor& descriptor) {
  glm::vec3 position;
  glm::quat rotation;
  glm::vec3 scale;
  glm::vec3 skew;
  glm::vec4 perspective;
  glm::decompose(descriptor.local_transform, scale, rotation, position, skew,
                 perspective);
  rotation =
      glm::normalize(rotation * glm::quat(descriptor.applied_rotation));
  return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
         glm::scale(glm::mat4(1.0f), scale * descriptor.applied_scale);
}

std::optional<std::vector<ModelInstantiator::Part>> ModelInstantiator::Parts(
    const InstanceModel& resolved, const ModelGraph& graph,
    const std::vector<MaterialHandle>* materials) {
  const std::vector<MaterialHandle>& handles =
      materials ? *materials : graph.material_handles;
  const glm::mat4 model = glm::scale(glm::mat4(1.0f), resolved.model_scale);
  std::vector<Part> parts;
  for (const auto& descriptor : resolved.descriptors) {
    const glm::mat4 matrix = model * _PartMatrix(descriptor);
    for (const int index : descriptor.mesh_indices) {
      const Mesh* mesh = graph.GetMesh(index);
      if (!mesh)
        continue;
      if (mesh->Skinned())
        return std::nullopt;
      const uint32_t material = mesh->MaterialIndex();
      parts.push_back({mesh,
                       material < handles.size() ? handles[material]
                                                 : INVALID_MATERIAL,
                       matrix});
    }
  }
  return parts;
}

Entity ModelInstantiator::CreatePartEntity(const ResolvedDescriptor& descriptor,
                                           const ModelGraph& graph,
                                           Entity parent,
                                           const std::vector<MaterialHandle>& materials,
                                           Entity animator) {
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

  // Attach mesh renderers: a single mesh to the part itself, several to a
  // child each. A skinned mesh ignores its node's transform (as glTF's): its
  // bones place it, so it hangs from the model's root
  const bool single = descriptor.mesh_indices.size() == 1;
  for (size_t i = 0; i < descriptor.mesh_indices.size(); ++i) {
    const Mesh* mesh = graph.GetMesh(descriptor.mesh_indices[i]);
    if (!mesh)
      continue;

    Entity holder = entity;
    if (!single || mesh->Skinned()) {
      std::string child_name =
          descriptor.descriptor_id + "_mesh_" + std::to_string(i);
      auto [child, child_transform] =
          ecs.CreateEntity(child_name, mesh->Skinned() ? parent : entity);

      // Identity transform — inherit parent's transform through hierarchy
      child_transform.modified_ = true;
      holder = child;
    }

    auto& renderer = ecs.Add<MeshRendererComponent>(holder);
    renderer.mesh_ = mesh;
    renderer.enabled_ = true;
    renderer.material_ = material(*mesh);
    if (mesh->Skinned())
      renderer.animator_ = animator;
  }

  return entity;
}
}  // namespace ProcModel

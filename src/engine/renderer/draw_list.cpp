#include "draw_list.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/model/mesh.h"

namespace {

struct BatchKey {
  const Mesh* mesh;
  MaterialHandle material;
  bool operator==(const BatchKey& other) const {
    return mesh == other.mesh && material == other.material;
  }
};

struct BatchKeyHash {
  size_t operator()(const BatchKey& key) const {
    return std::hash<const void*>()(key.mesh) ^
           (std::hash<MaterialHandle>()(key.material) * 0x9e3779b97f4a7c15ull);
  }
};

constexpr uint16_t instance_stride = sizeof(glm::mat4);

// The frustum's planes (inside where dot(xyz, p) + w >= 0). The near plane
// takes depth from -1, so it holds for 0..1 and -1..1 projections alike (a
// little loose for 0..1)
std::array<glm::vec4, 6> FrustumPlanes(const glm::mat4& m) {
  const glm::mat4 t = glm::transpose(m);  // rows of m
  std::array<glm::vec4, 6> planes = {t[3] + t[0], t[3] - t[0], t[3] + t[1],
                                     t[3] - t[1], t[3] + t[2], t[3] - t[2]};
  for (glm::vec4& plane : planes)
    plane /= glm::length(glm::vec3(plane));
  return planes;
}

bool InFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& center,
               float radius) {
  for (const glm::vec4& plane : planes)
    if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
      return false;
  return true;
}

}  // namespace

void DrawList::Build(std::initializer_list<Frustum> frusta,
                     const std::vector<Entity>& apart, bool instanced) {
  auto& registry = MaterialRegistry::Instance();
  struct Group {
    BatchKey key;
    bool instanced;  // its material has an instanced program
    std::vector<Single> members;
  };
  struct Fill {
    DrawList* list;
    std::array<glm::vec4, 6> planes;
    std::vector<Group> groups;
    std::unordered_map<BatchKey, size_t, BatchKeyHash> group_of;
  };
  std::vector<Fill> fills;
  for (const Frustum& frustum : frusta) {
    frustum.list->batches.clear();
    frustum.list->singles.clear();
    fills.push_back({frustum.list, FrustumPlanes(frustum.view_projection)});
  }

  for (auto [entity, transform, renderer] : ECS::Main().GetRenderQueue()) {
    if (!renderer.enabled_ || !renderer.mesh_)
      continue;
    const glm::mat4& model = transform.model_;
    const Mesh& mesh = *renderer.mesh_;
    const glm::vec3 center = model * glm::vec4(mesh.BoundsCenter(), 1.0f);
    const float radius =
        mesh.BoundsRadius() *
        glm::sqrt(glm::max(
            glm::max(glm::dot(model[0], model[0]), glm::dot(model[1], model[1])),
            glm::dot(model[2], model[2])));
    const Single single{entity, &model, renderer.mesh_, renderer.material_,
                        renderer.pose_};
    // Skinned meshes have their own bones: never instanced
    const bool alone =
        !instanced || renderer.pose_ ||
        std::find(apart.begin(), apart.end(), entity) != apart.end();

    for (Fill& fill : fills) {
      if (!InFrustum(fill.planes, center, radius))
        continue;
      if (alone) {
        fill.list->singles.push_back(single);
        continue;
      }
      // The material is looked up once per group, not per mesh
      const BatchKey key{renderer.mesh_, renderer.material_};
      auto [it, added] = fill.group_of.try_emplace(key, fill.groups.size());
      if (added) {
        const Material* material = registry.Get(renderer.material_);
        fill.groups.push_back(
            {key, material && bgfx::isValid(material->instanced_program), {}});
      }
      Group& group = fill.groups[it->second];
      if (group.instanced)
        group.members.push_back(single);
      else
        fill.list->singles.push_back(single);
    }
  }

  // Each shared group's matrices into an instance buffer; lone meshes and what
  // the frame's buffer can't hold draw one by one
  for (Fill& fill : fills) {
    for (const Group& group : fill.groups) {
      const uint32_t count = static_cast<uint32_t>(group.members.size());
      const uint32_t room =
          count < 2 ? 0
                    : bgfx::getAvailInstanceDataBuffer(count, instance_stride);
      if (room > 0) {
        Batch batch{group.key.mesh, group.key.material, {}};
        bgfx::allocInstanceDataBuffer(&batch.instances, room, instance_stride);
        for (uint32_t i = 0; i < room; ++i)
          std::memcpy(batch.instances.data + i * instance_stride,
                      group.members[i].model, instance_stride);
        fill.list->batches.push_back(batch);
      }
      fill.list->singles.insert(fill.list->singles.end(),
                                group.members.begin() + room,
                                group.members.end());
    }
  }
}

void DrawList::Bind(const Batch& batch) {
  bgfx::setInstanceDataBuffer(&batch.instances);
  batch.mesh->Bind();
}

bool DrawList::SetBones(const Mesh& mesh, const glm::mat4* pose) {
  if (!pose || !mesh.Skinned())
    return false;
  const bgfx::UniformHandle bones =
      static_cast<GraphicsRenderer&>(EngineContext::Renderer())
          .CreateUniform("u_bones", bgfx::UniformType::Mat4, Mesh::max_bones);
  glm::mat4 matrices[Mesh::max_bones];
  mesh.SkinMatrices(pose, matrices);
  bgfx::setUniform(bones, matrices, static_cast<uint16_t>(mesh.BoneCount()));
  return true;
}

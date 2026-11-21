#include "draw_list.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

#include "engine/core/logger.h"
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

// The frustum's planes (inside where dot(xyz, p) + w >= 0), from a
// left-handed projection with depth 0..1 (Transformation::Projection)
std::array<glm::vec4, 6> FrustumPlanes(const glm::mat4& m) {
  const glm::mat4 t = glm::transpose(m);  // rows of m
  std::array<glm::vec4, 6> planes = {t[3] + t[0], t[3] - t[0], t[3] + t[1],
                                     t[3] - t[1], t[2],        t[3] - t[2]};
  for (glm::vec4& plane : planes)
    plane /= glm::length(glm::vec3(plane));
  return planes;
}

bool InFrustum(const std::array<glm::vec4, 6>& planes, const glm::mat4& model,
               const Mesh& mesh) {
  const glm::vec3 center = model * glm::vec4(mesh.BoundsCenter(), 1.0f);
  const float scale = glm::sqrt(glm::max(
      glm::max(glm::dot(model[0], model[0]), glm::dot(model[1], model[1])),
      glm::dot(model[2], model[2])));
  const float radius = mesh.BoundsRadius() * scale;
  for (const glm::vec4& plane : planes)
    if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
      return false;
  return true;
}

}  // namespace

void DrawList::Build(const std::vector<Entity>& apart,
                     const glm::mat4& view_projection, bool instanced) {
  batches.clear();
  singles.clear();

  // TODO: shadow casters outside the camera's view are culled too
  const std::array<glm::vec4, 6> planes = FrustumPlanes(view_projection);

  auto& registry = MaterialRegistry::Instance();
  struct Group {
    BatchKey key;
    std::vector<Single> members;
  };
  std::vector<Group> groups;
  std::unordered_map<BatchKey, size_t, BatchKeyHash> group_of;

  for (auto [entity, transform, renderer] : ECS::Main().GetRenderQueue()) {
    if (!renderer.enabled_ || !renderer.mesh_ ||
        !InFrustum(planes, transform.model_, *renderer.mesh_))
      continue;
    const Single single{entity, &transform.model_, renderer.mesh_,
                        renderer.material_};
    const Material* material = registry.Get(renderer.material_);
    if (!instanced || !material ||
        !bgfx::isValid(material->instanced_program) ||
        std::find(apart.begin(), apart.end(), entity) != apart.end()) {
      singles.push_back(single);
      continue;
    }
    const BatchKey key{renderer.mesh_, renderer.material_};
    auto [it, added] = group_of.try_emplace(key, groups.size());
    if (added)
      groups.push_back({key, {}});
    groups[it->second].members.push_back(single);
  }

  // Each shared group's matrices into an instance buffer; lone meshes and what
  // the frame's buffer can't hold draw one by one
  for (const Group& group : groups) {
    const uint32_t count = static_cast<uint32_t>(group.members.size());
    const uint32_t room =
        count < 2 ? 0 : bgfx::getAvailInstanceDataBuffer(count, instance_stride);
    if (room > 0) {
      Batch batch{group.key.mesh, group.key.material, {}};
      bgfx::allocInstanceDataBuffer(&batch.instances, room, instance_stride);
      for (uint32_t i = 0; i < room; ++i)
        std::memcpy(batch.instances.data + i * instance_stride,
                    group.members[i].model, instance_stride);
      batches.push_back(batch);
    }
    singles.insert(singles.end(), group.members.begin() + room,
                   group.members.end());
  }
}

void DrawList::Bind(const Batch& batch) {
  bgfx::setInstanceDataBuffer(&batch.instances);
  batch.mesh->Bind();
}

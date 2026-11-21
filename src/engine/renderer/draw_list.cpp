#include "draw_list.h"

#include <algorithm>
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

}  // namespace

void DrawList::Build(const std::vector<Entity>& apart, bool instanced) {
  batches.clear();
  singles.clear();

  auto& registry = MaterialRegistry::Instance();
  struct Group {
    BatchKey key;
    std::vector<Single> members;
  };
  std::vector<Group> groups;
  std::unordered_map<BatchKey, size_t, BatchKeyHash> group_of;

  for (auto [entity, transform, renderer] : ECS::Main().GetRenderQueue()) {
    if (!renderer.enabled_ || !renderer.mesh_)
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

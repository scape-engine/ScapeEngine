#include "draw_list.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <unordered_map>

#include "engine/ecs/components/water_surface_tag.h"
#include "engine/animation/animation.h"
#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/ecs/components/instanced_mesh_component.h"
#include "engine/fade/fade_system.h"
#include "engine/renderer/graphics_renderer.h"
#include "engine/renderer/model/mesh.h"

namespace {

struct BatchKey {
  const Mesh* mesh;
  MaterialHandle material;
  bool mirrored;
  // The draw settings' fade of the model it stands for (a LOD mesh can serve
  // several)
  float fade_start;
  float fade_end;
  bool operator==(const BatchKey& other) const {
    return mesh == other.mesh && material == other.material &&
           mirrored == other.mirrored && fade_start == other.fade_start &&
           fade_end == other.fade_end;
  }
};

struct BatchKeyHash {
  size_t operator()(const BatchKey& key) const {
    return (std::hash<const void*>()(key.mesh) ^
            (std::hash<MaterialHandle>()(key.material) * 0x9e3779b97f4a7c15ull)) +
           key.mirrored;
  }
};

constexpr uint16_t instance_stride = sizeof(glm::mat4);

// Instanced meshes smaller than this share of the view's height are not drawn
// TODO: mesh LODs, so distant instances thin out instead of popping
constexpr float min_screen_size = 0.01f;

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

// Whether a sphere of this size anywhere in a bigger one projects under
// min_screen_size: the projection's y scale over the depth (w) of the bigger
// sphere's nearest point. An orthographic w is 1 everywhere
bool TooSmall(const glm::mat4& view_projection, const glm::vec3& center,
              float radius, float size) {
  const glm::mat4 t = glm::transpose(view_projection);
  const float w = glm::dot(t[3], glm::vec4(center, 1.0f)) -
                  radius * glm::length(glm::vec3(t[3]));
  return w > 0.0f &&
         size * glm::length(glm::vec3(t[1])) < min_screen_size * w;
}

// An instanced mesh's world matrices, its parts' bounds and its buffer from
// the entity's model matrix
void Upload(InstancedMeshComponent& set, const glm::mat4& model) {
  static bgfx::VertexLayout layout;
  if (layout.getStride() == 0)
    layout.begin()
        .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
        .end();
  // Each part's mirrored instances last, drawn as a batch of their own
  for (InstancedMeshComponent::Part& part : set.parts_) {
    const auto first = set.locals_.begin() + part.first;
    part.mirrored = static_cast<uint32_t>(
        first + part.count -
        std::stable_partition(first, first + part.count,
                              [](const glm::mat4& m) {
                                return !DrawList::Mirrored(m);
                              }));
  }
  set.model_ = model;
  set.worlds_.resize(set.locals_.size());
  for (size_t i = 0; i < set.locals_.size(); ++i)
    set.worlds_[i] = model * set.locals_[i];
  for (InstancedMeshComponent::Part& part : set.parts_) {
    glm::vec3 low(FLT_MAX), high(-FLT_MAX);
    float reach = 0.0f;
    for (uint32_t i = part.first; i < part.first + part.count; ++i) {
      const glm::mat4& world = set.worlds_[i];
      const glm::vec3 center =
          world * glm::vec4(part.mesh->BoundsCenter(), 1.0f);
      low = glm::min(low, center);
      high = glm::max(high, center);
      reach = glm::max(
          reach,
          part.mesh->BoundsRadius() *
              glm::sqrt(glm::max(glm::max(glm::dot(world[0], world[0]),
                                          glm::dot(world[1], world[1])),
                                 glm::dot(world[2], world[2]))));
    }
    part.center = (low + high) * 0.5f;
    part.radius = glm::length(high - low) * 0.5f + reach;
    part.reach = reach;
  }
  set.buffer_ = InstancedMeshComponent::Buffer();
  set.buffer_.handle = bgfx::createVertexBuffer(
      bgfx::copy(set.worlds_.data(),
                 static_cast<uint32_t>(set.worlds_.size() * sizeof(glm::mat4))),
      layout);
}

}  // namespace

void DrawList::Build(std::initializer_list<Frustum> frusta,
                     const glm::vec3& viewer, const std::vector<Entity>& apart,
                     bool instanced) {
  auto& registry = MaterialRegistry::Instance();
  struct Group {
    BatchKey key;
    bool instanced;  // its material has an instanced program
    std::vector<Single> members;
  };
  struct Fill {
    DrawList* list;
    std::array<glm::vec4, 6> planes;
    glm::mat4 view_projection;
    std::vector<Group> groups;
    std::unordered_map<BatchKey, size_t, BatchKeyHash> group_of;
  };
  std::vector<Fill> fills;
  for (const Frustum& frustum : frusta) {
    frustum.list->batches.clear();
    frustum.list->singles.clear();
    fills.push_back({frustum.list, FrustumPlanes(frustum.view_projection),
                     frustum.view_projection});
  }

  for (auto [entity, transform, renderer] : ECS::Main().GetRenderQueue()) {
    // Water draws in its own pass (SceneViewPipeline)
    if (!renderer.enabled_ || !renderer.mesh_ ||
        ECS::Main().Has<WaterSurfaceTag>(entity))
      continue;
    const float visibility = FadeSystem::Visibility(ECS::Main(), entity);
    if (visibility <= 0.0f)
      continue;
    const glm::mat4& model = transform.model_;
    const Mesh& mesh = *renderer.mesh_;
    // The model's draw settings: past its fade, nothing; farther, a coarser
    // mesh
    const float distance = glm::distance(viewer, glm::vec3(model[3]));
    if (mesh.FadeEnd() > mesh.FadeStart() && distance > mesh.FadeEnd())
      continue;
    const Mesh& drawn = mesh.ForDistance(distance);
    const glm::vec3 center = model * glm::vec4(mesh.BoundsCenter(), 1.0f);
    const float radius =
        mesh.BoundsRadius() *
        glm::sqrt(glm::max(
            glm::max(glm::dot(model[0], model[0]), glm::dot(model[1], model[1])),
            glm::dot(model[2], model[2])));
    uint32_t pose_count = 0;
    const glm::mat4* pose =
        renderer.mesh_->Skinned()
            ? Animation::Pose(ECS::Main(), renderer.animator_, pose_count)
            : nullptr;
    const Single single{
        entity, &model,     &drawn,
        renderer.material_, pose,
        pose_count,         {viewer, mesh.FadeStart(), mesh.FadeEnd(), visibility}};
    // Skinned meshes have their own bones and fading ones their own
    // visibility: never instanced
    const bool alone =
        !instanced || pose || visibility < 1.0f ||
        std::find(apart.begin(), apart.end(), entity) != apart.end();

    for (Fill& fill : fills) {
      if (!InFrustum(fill.planes, center, radius))
        continue;
      if (alone) {
        fill.list->singles.push_back(single);
        continue;
      }
      // The material is looked up once per group, not per mesh
      const BatchKey key{&drawn, renderer.material_, Mirrored(model),
                         mesh.FadeStart(), mesh.FadeEnd()};
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

  // Instanced meshes: one cull and one draw for all of a part's instances
  // (one by one without an instanced program)
  for (auto [entity, transform, set] :
       ECS::Main().View<TransformComponent, InstancedMeshComponent>().each()) {
    if (!set.enabled_ || set.locals_.empty())
      continue;
    const float visibility = FadeSystem::Visibility(ECS::Main(), entity);
    if (visibility <= 0.0f)
      continue;
    if (set.model_ != transform.model_ ||
        set.worlds_.size() != set.locals_.size())
      Upload(set, transform.model_);
    for (const InstancedMeshComponent::Part& part : set.parts_) {
      if (!part.mesh || part.count == 0)
        continue;
      const Material* material = registry.Get(part.material);
      const bool batched = instanced && material &&
                           bgfx::isValid(material->instanced_program) &&
                           bgfx::isValid(set.buffer_.handle);
      // None of the part's instances within its draw distance (the part's
      // own, else its mesh's draw settings)
      // TODO: LODs for instanced parts (Mesh::ForDistance per instance)
      const bool own = part.fade_end > part.fade_start;
      const float fade_start = own ? part.fade_start : part.mesh->FadeStart();
      const float fade_end = own ? part.fade_end : part.mesh->FadeEnd();
      const Fade fade{viewer, fade_start, fade_end, visibility};
      const bool limited = fade_end > fade_start;
      if (limited &&
          glm::distance(viewer, part.center) - part.radius > fade_end)
        continue;
      for (Fill& fill : fills) {
        if (!InFrustum(fill.planes, part.center, part.radius) ||
            TooSmall(fill.view_projection, part.center, part.radius,
                     part.reach))
          continue;
        if (batched) {
          // the entity's own matrix mirrors both runs alike
          const bool flip = Mirrored(transform.model_);
          const uint32_t plain = part.count - part.mirrored;
          if (plain > 0)
            fill.list->batches.push_back({part.mesh, part.material, {},
                                          set.buffer_.handle, part.first,
                                          plain, flip, fade});
          if (part.mirrored > 0)
            fill.list->batches.push_back({part.mesh, part.material, {},
                                          set.buffer_.handle,
                                          part.first + plain, part.mirrored,
                                          !flip, fade});
          continue;
        }
        // One by one, those within the distance
        for (uint32_t i = part.first; i < part.first + part.count; ++i)
          if (!limited ||
              glm::distance(viewer, glm::vec3(set.worlds_[i][3])) <= fade_end)
            fill.list->singles.push_back({entity, &set.worlds_[i], part.mesh,
                                          part.material, nullptr, 0, fade});
      }
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
        batch.mirrored = group.key.mirrored;
        batch.fade = {viewer, group.key.fade_start, group.key.fade_end, 1.0f};
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

void DrawList::SetFade(const Fade& fade) {
  auto& graphics = static_cast<GraphicsRenderer&>(EngineContext::Renderer());
  static const bgfx::UniformHandle viewer =
      graphics.CreateUniform("u_fadeViewer", bgfx::UniformType::Vec4);
  static const bgfx::UniformHandle range =
      graphics.CreateUniform("u_fadeRange", bgfx::UniformType::Vec4);
  const glm::vec4 at(fade.viewer, 0.0f);
  const glm::vec4 values(fade.start, fade.end, fade.visibility, 0.0f);
  bgfx::setUniform(viewer, &at);
  bgfx::setUniform(range, &values);
}

void DrawList::Bind(const Batch& batch) {
  SetFade(batch.fade);
  if (bgfx::isValid(batch.buffer))
    bgfx::setInstanceDataBuffer(batch.buffer, batch.first, batch.count);
  else
    bgfx::setInstanceDataBuffer(&batch.instances);
  batch.mesh->Bind();
}

Content::MaterialBlend DrawList::BlendOf(MaterialHandle material) {
  const Material* found = MaterialRegistry::Instance().Get(material);
  return found ? found->blend : Content::MaterialBlend::Opaque;
}

bool DrawList::Mirrored(const glm::mat4& model) {
  return glm::determinant(glm::mat3(model)) < 0.0f;
}

uint64_t DrawList::Cull(MaterialHandle material, bool mirrored) {
  const Material* found = MaterialRegistry::Instance().Get(material);
  if (found && found->double_sided)
    return 0;
  return mirrored ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
}

bool DrawList::SetBones(const Mesh& mesh, const glm::mat4* pose,
                        uint32_t pose_count) {
  if (!pose || !mesh.Skinned())
    return false;
  const bgfx::UniformHandle bones =
      static_cast<GraphicsRenderer&>(EngineContext::Renderer())
          .CreateUniform("u_bones", bgfx::UniformType::Mat4, Mesh::max_bones);
  glm::mat4 matrices[Mesh::max_bones];
  mesh.SkinMatrices(pose, pose_count, matrices);
  bgfx::setUniform(bones, matrices, static_cast<uint16_t>(mesh.BoneCount()));
  return true;
}

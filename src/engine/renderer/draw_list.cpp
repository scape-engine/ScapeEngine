#include "draw_list.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <numeric>
#include <unordered_map>

#include "engine/ecs/components/water_surface_tag.h"
#include "engine/animation/animation.h"
#include "engine/context/engine_context.h"
#include "engine/core/logger.h"
#include "engine/diagnostics/frame_stats.h"
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
  glm::vec4 lod;  // the LOD's blends in its model's chain
  bool operator==(const BatchKey& other) const {
    return mesh == other.mesh && material == other.material &&
           mirrored == other.mirrored && fade_start == other.fade_start &&
           fade_end == other.fade_end && lod == other.lod;
  }
};

struct BatchKeyHash {
  size_t operator()(const BatchKey& key) const {
    return (std::hash<const void*>()(key.mesh) ^
            (std::hash<MaterialHandle>()(key.material) * 0x9e3779b97f4a7c15ull)) +
           key.mirrored;
  }
};

// An instance: its world matrix, then the point it fades and switches LODs by
// (InstancedMeshComponent::anchors_; w unused)
constexpr uint16_t instance_stride = sizeof(glm::mat4) + sizeof(glm::vec4);

void WriteInstance(uint8_t* out, const glm::mat4& world,
                   const glm::vec3& anchor) {
  const glm::vec4 at(anchor, 1.0f);
  std::memcpy(out, &world, sizeof(world));
  std::memcpy(out + sizeof(world), &at, sizeof(at));
}

// Instanced meshes smaller than this share of the view's height are not drawn
// TODO: mesh LODs, so distant instances thin out instead of popping
constexpr float min_screen_size = 0.01f;

// The frustum's planes (inside where dot(xyz, p) + w >= 0). Depth 0..1, in
// either order: the camera's is reversed, the light's is not
std::array<glm::vec4, 6> FrustumPlanes(const glm::mat4& m) {
  const glm::mat4 t = glm::transpose(m);  // rows of m
  std::array<glm::vec4, 6> planes = {t[3] + t[0], t[3] - t[0], t[3] + t[1],
                                     t[3] - t[1], t[2],        t[3] - t[2]};
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

// Whether the part draws as another LOD of its mesh, starts or stops, or
// blends into either, somewhere between these distances
bool Switches(const InstancedMeshComponent::Part& part, float from, float to) {
  const auto within = [&](float at, float blend) {
    return at > from && at * (1.0f - blend) <= to;
  };
  for (const Mesh::Lod& lod : part.mesh->Lods())
    if (lod.mesh && within(lod.distance, part.mesh->LodFade()))
      return true;
  return (part.from > 0.0f && within(part.from, part.blend)) ||
         (part.until > 0.0f && within(part.until, part.blend));
}

// The part's mesh LODs at `distance` (Mesh::ForDistance) within its draw
// range: none outside it, blending in and out at its ends
int PartLods(const InstancedMeshComponent::Part& part, float distance,
             Mesh::Drawn out[2]) {
  const float starts = part.from * (1.0f - part.blend);
  const float ends = part.until * (1.0f - part.blend);
  if ((part.from > 0.0f && distance < starts) ||
      (part.until > 0.0f && distance >= part.until))
    return 0;
  int count = part.mesh->ForDistance(distance, out);
  // A LOD the range ends before never shows
  if (count == 2 && part.until > 0.0f && out[1].lod.y >= part.until)
    count = 1;
  for (int l = 0; l < count; ++l) {
    glm::vec4& lod = out[l].lod;
    if (part.from > 0.0f && lod.y <= part.from) {
      lod.x = starts;
      lod.y = part.from;
    }
    if (part.until > 0.0f && (lod.w <= 0.0f || lod.w > part.until)) {
      lod.z = ends;
      lod.w = part.until;
    }
  }
  return count;
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
        .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)
        .end();
  const bool anchored = set.anchors_.size() == set.locals_.size();
  // Each part's mirrored instances last, drawn as a batch of their own (their
  // anchors moved alike)
  for (InstancedMeshComponent::Part& part : set.parts_) {
    std::vector<uint32_t> order(part.count);
    std::iota(order.begin(), order.end(), part.first);
    part.mirrored = static_cast<uint32_t>(
        order.end() -
        std::stable_partition(order.begin(), order.end(), [&](uint32_t i) {
          return !DrawList::Mirrored(set.locals_[i]);
        }));
    std::vector<glm::mat4> locals;
    std::vector<glm::vec3> anchors;
    for (const uint32_t i : order) {
      locals.push_back(set.locals_[i]);
      if (anchored)
        anchors.push_back(set.anchors_[i]);
    }
    std::copy(locals.begin(), locals.end(), set.locals_.begin() + part.first);
    std::copy(anchors.begin(), anchors.end(),
              set.anchors_.begin() + (anchored ? part.first : 0));
  }
  set.model_ = model;
  set.worlds_.resize(set.locals_.size());
  set.world_anchors_.resize(set.locals_.size());
  std::vector<uint8_t> instances(set.locals_.size() * instance_stride);
  for (size_t i = 0; i < set.locals_.size(); ++i) {
    set.worlds_[i] = model * set.locals_[i];
    set.world_anchors_[i] =
        anchored ? glm::vec3(model * glm::vec4(set.anchors_[i], 1.0f))
                 : glm::vec3(set.worlds_[i][3]);
    WriteInstance(&instances[i * instance_stride], set.worlds_[i],
                  set.world_anchors_[i]);
  }
  for (InstancedMeshComponent::Part& part : set.parts_) {
    glm::vec3 low(FLT_MAX), high(-FLT_MAX);
    float reach = 0.0f;
    for (uint32_t i = part.first; i < part.first + part.count; ++i) {
      const glm::mat4& world = set.worlds_[i];
      const glm::vec3 center =
          world * glm::vec4(part.mesh->BoundsCenter(), 1.0f);
      // The sphere holds the anchors too: its distances bound theirs
      low = glm::min(low, glm::min(center, set.world_anchors_[i]));
      high = glm::max(high, glm::max(center, set.world_anchors_[i]));
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
      bgfx::copy(instances.data(), static_cast<uint32_t>(instances.size())),
      layout);
}

}  // namespace

void DrawList::Build(std::initializer_list<Frustum> frusta,
                     const glm::vec3& viewer, const std::vector<Entity>& apart,
                     bool instanced) {
  Diagnostics::ScopedSection section("Draw list");
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
    // mesh (two while one blends into the next)
    const float distance = glm::distance(viewer, glm::vec3(model[3]));
    if (mesh.FadeEnd() > mesh.FadeStart() && distance > mesh.FadeEnd())
      continue;
    Mesh::Drawn drawn[2];
    const int lods = mesh.ForDistance(distance, drawn);
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
    // Skinned meshes have their own bones and fading ones their own
    // visibility: never instanced
    const bool alone =
        !instanced || pose || visibility < 1.0f ||
        std::find(apart.begin(), apart.end(), entity) != apart.end();

    for (Fill& fill : fills) {
      if (!InFrustum(fill.planes, center, radius))
        continue;
      for (int l = 0; l < lods; ++l) {
        const Single single{entity, &model, drawn[l].mesh, renderer.material_,
                            pose, pose_count,
                            {viewer, mesh.FadeStart(), mesh.FadeEnd(),
                             visibility, drawn[l].lod}};
        if (alone) {
          fill.list->singles.push_back(single);
          continue;
        }
        // The material is looked up once per group, not per mesh
        const BatchKey key{drawn[l].mesh, renderer.material_, Mirrored(model),
                           mesh.FadeStart(), mesh.FadeEnd(), drawn[l].lod};
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
      const bool own = part.fade_end > part.fade_start;
      const float fade_start = own ? part.fade_start : part.mesh->FadeStart();
      const float fade_end = own ? part.fade_end : part.mesh->FadeEnd();
      const Fade fade{viewer, fade_start, fade_end, visibility};
      // As a LOD of the part's mesh
      const auto lod_fade = [&](const glm::vec4& lod) {
        Fade blended = fade;
        blended.lod = lod;
        return blended;
      };
      const bool limited = fade_end > fade_start;
      const float distance = glm::distance(viewer, part.center);
      if (limited && distance - part.radius > fade_end)
        continue;
      // Its mesh's LODs: one for all of them when no LOD switches or blends
      // within its sphere, else one or two per instance, their matrices
      // sorted by LOD into the frame's instance buffer (once, for every
      // frustum)
      const float closest = std::max(distance - part.radius, 0.0f);
      const bool split = Switches(part, closest, distance + part.radius);
      Mesh::Drawn drawn[2];
      if (!split && PartLods(part, closest, drawn) == 0)
        continue;
      // the entity's own matrix mirrors both runs alike
      const bool flip = Mirrored(transform.model_);
      const uint32_t plain = part.count - part.mirrored;
      struct Run {
        Mesh::Drawn drawn;
        bool mirrored;
        std::vector<uint32_t> at;  // into worlds_
        bgfx::InstanceDataBuffer instances{};
        uint32_t room = 0;  // how many of them the buffer holds
      };
      std::vector<Run> runs;
      if (batched && split) {
        for (uint32_t i = part.first; i < part.first + part.count; ++i) {
          const float from = glm::distance(viewer, set.world_anchors_[i]);
          if (limited && from > fade_end)
            continue;
          Mesh::Drawn lods[2];
          const int count = PartLods(part, from, lods);
          const bool mirrored = i >= part.first + plain ? !flip : flip;
          for (int l = 0; l < count; ++l) {
            auto run =
                std::find_if(runs.begin(), runs.end(), [&](const Run& r) {
                  return r.drawn.mesh == lods[l].mesh && r.mirrored == mirrored;
                });
            if (run == runs.end())
              run = runs.insert(runs.end(), Run{lods[l], mirrored});
            run->at.push_back(i);
          }
        }
        for (Run& run : runs) {
          run.room = bgfx::getAvailInstanceDataBuffer(
              static_cast<uint32_t>(run.at.size()), instance_stride);
          if (run.room == 0)
            continue;
          bgfx::allocInstanceDataBuffer(&run.instances, run.room,
                                        instance_stride);
          for (uint32_t i = 0; i < run.room; ++i)
            WriteInstance(run.instances.data + i * instance_stride,
                          set.worlds_[run.at[i]],
                          set.world_anchors_[run.at[i]]);
        }
      }
      for (Fill& fill : fills) {
        if (!InFrustum(fill.planes, part.center, part.radius) ||
            TooSmall(fill.view_projection, part.center, part.radius,
                     part.reach))
          continue;
        if (batched && split) {
          // What the frame's buffer can't hold draws one by one
          for (const Run& run : runs) {
            if (run.room > 0) {
              Batch batch{run.drawn.mesh, part.material, run.instances};
              batch.mirrored = run.mirrored;
              batch.fade = lod_fade(run.drawn.lod);
              fill.list->batches.push_back(batch);
            }
            for (size_t i = run.room; i < run.at.size(); ++i)
              fill.list->singles.push_back({entity, &set.worlds_[run.at[i]],
                                            run.drawn.mesh, part.material,
                                            nullptr, 0, lod_fade(run.drawn.lod)});
          }
          continue;
        }
        if (batched) {
          if (plain > 0)
            fill.list->batches.push_back({drawn[0].mesh, part.material, {},
                                          set.buffer_.handle, part.first,
                                          plain, flip, lod_fade(drawn[0].lod)});
          if (part.mirrored > 0)
            fill.list->batches.push_back({drawn[0].mesh, part.material, {},
                                          set.buffer_.handle,
                                          part.first + plain, part.mirrored,
                                          !flip, lod_fade(drawn[0].lod)});
          continue;
        }
        // One by one, those within the distance
        for (uint32_t i = part.first; i < part.first + part.count; ++i) {
          const float from = glm::distance(viewer, set.world_anchors_[i]);
          if (limited && from > fade_end)
            continue;
          Mesh::Drawn lods[2];
          const int count = PartLods(part, from, lods);
          for (int l = 0; l < count; ++l)
            fill.list->singles.push_back({entity, &set.worlds_[i],
                                          lods[l].mesh, part.material, nullptr,
                                          0, lod_fade(lods[l].lod)});
        }
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
        batch.fade = {viewer, group.key.fade_start, group.key.fade_end, 1.0f,
                      group.key.lod};
        bgfx::allocInstanceDataBuffer(&batch.instances, room, instance_stride);
        for (uint32_t i = 0; i < room; ++i)
          WriteInstance(batch.instances.data + i * instance_stride,
                        *group.members[i].model,
                        glm::vec3((*group.members[i].model)[3]));
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
  static const bgfx::UniformHandle lod =
      graphics.CreateUniform("u_fadeLod", bgfx::UniformType::Vec4);
  const glm::vec4 at(fade.viewer, 0.0f);
  const glm::vec4 values(fade.start, fade.end, fade.visibility, 0.0f);
  bgfx::setUniform(viewer, &at);
  bgfx::setUniform(range, &values);
  bgfx::setUniform(lod, &fade.lod);
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

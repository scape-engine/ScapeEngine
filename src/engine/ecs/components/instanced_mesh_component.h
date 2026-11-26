#ifndef INSTANCED_MESH_COMPONENT_H
#define INSTANCED_MESH_COMPONENT_H

#include <cstdint>
#include <utility>
#include <vector>

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/mesh.h"

/* ───────────────────────────── INSTANCED MESH ──────────────────────────── */
// Meshes drawn at many transforms relative to the entity (e.g. a region's
// building pieces), like an instanced static mesh: each part is a mesh and
// material drawn at a run of the transforms. The draw list keeps their world
// matrices in one GPU buffer, made again only when the entity moves, and culls
// and draws each part as one. A part whose instances would all be under a
// pixel or so on screen is not drawn
struct InstancedMeshComponent {
  struct Part {
    const Mesh* mesh = nullptr;
    MaterialHandle material = INVALID_MATERIAL;
    uint32_t first = 0;  // into locals_
    uint32_t count = 0;
    // How far from the viewer its instances draw: each dithers out from
    // fade_start to fade_end by its anchor, and none draw past it
    // (fade_end <= fade_start: no limit). The entity's FadeComponent fades
    // them all
    float fade_start = 0.0f;
    float fade_end = 0.0f;
    // It draws from `from` to `until` metres from the viewer, by each
    // instance's anchor (0: no bound), blending in and out over `blend` of
    // each: an imposter's cards and the meshes they stand in for
    float from = 0.0f;
    float until = 0.0f;
    float blend = 0.0f;

    // Kept by the draw list: how many of the run are mirrored (moved last), a
    // sphere around its instances and the largest one's radius
    uint32_t mirrored = 0;
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    float reach = 0.0f;
  };
  std::vector<Part> parts_;
  std::vector<glm::mat4> locals_;
  // Per transform, the point its distance from the viewer is measured from
  // (fades, LODs, draw ranges), relative to the entity: its object's origin,
  // so a model's parts switch together. Empty: each transform's own origin
  std::vector<glm::vec3> anchors_;
  bool enabled_{true};

  // The meshes of the object `instance` is part of (the instances sharing its
  // anchor; without anchors, it alone), not its imposter's card: each its
  // part and transform
  struct ObjectDraw {
    const Part* part;
    uint32_t instance;
  };
  std::vector<ObjectDraw> ObjectOf(uint32_t instance) const {
    std::vector<ObjectDraw> draws;
    const bool anchored = anchors_.size() == locals_.size();
    for (const Part& part : parts_) {
      if (part.from > 0.0f)
        continue;
      for (uint32_t i = part.first; i < part.first + part.count; ++i)
        if (anchored ? anchors_[i] == anchors_[instance] : i == instance)
          draws.push_back({&part, i});
    }
    return draws;
  }

  // Owns a GPU buffer; moves with the component
  struct Buffer {
    bgfx::VertexBufferHandle handle = BGFX_INVALID_HANDLE;
    Buffer() = default;
    Buffer(Buffer&& other) noexcept : handle(other.handle) {
      other.handle = BGFX_INVALID_HANDLE;
    }
    Buffer& operator=(Buffer&& other) noexcept {
      std::swap(handle, other.handle);
      return *this;
    }
    ~Buffer() {
      if (bgfx::isValid(handle))
        bgfx::destroy(handle);
    }
  };

  // Kept by the draw list: the world matrices and anchors, the entity's model
  // matrix they were made from and their buffer
  std::vector<glm::mat4> worlds_;
  std::vector<glm::vec3> world_anchors_;
  glm::mat4 model_{0.0f};
  Buffer buffer_;
};

#endif  // INSTANCED_MESH_COMPONENT_H

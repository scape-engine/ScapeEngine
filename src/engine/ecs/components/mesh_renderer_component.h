#ifndef MESH_RENDERER_COMPONENT_H
#define MESH_RENDERER_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/mesh.h"

/* ────────────────────────────── MESH RENDERER ──────────────────────────── */
struct MeshRendererComponent {
  const Mesh* mesh_{nullptr};
  MaterialHandle material_{INVALID_MATERIAL};
  bool enabled_{true};

  // Where mesh_ comes from: a project-relative model path, or
  // "primitive:<shape>"
  std::string source_;
  // Index of the mesh within the source model
  uint32_t mesh_index_{0};
};

#endif  // MESH_RENDERER_COMPONENT_H
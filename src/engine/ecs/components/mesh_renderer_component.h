#ifndef MESH_RENDERER_COMPONENT_H
#define MESH_RENDERER_COMPONENT_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/renderer/material/material_registry.h"
#include "engine/renderer/model/mesh.h"

/* ────────────────────────────── MESH RENDERER ──────────────────────────── */
struct MeshRendererComponent {
  const Mesh* mesh_{nullptr};
  MaterialHandle material_{INVALID_MATERIAL};
  bool enabled_{true};
};

#endif  // MESH_RENDERER_COMPONENT_H
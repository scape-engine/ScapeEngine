#ifndef MESH_OWNER_COMPONENT_H
#define MESH_OWNER_COMPONENT_H

#include <memory>
#include <vector>

// Keeps meshes made for an entity's renderers (e.g. a procedural model's
// deformed parts) alive while it lives; shared with other entities drawing
// the same ones
struct MeshOwnerComponent {
  std::vector<std::shared_ptr<const void>> meshes_;
};

#endif  // MESH_OWNER_COMPONENT_H

#ifndef GROUND_COMPONENT_H
#define GROUND_COMPONENT_H

#include <memory>

#include "engine/geometry/ground/ground_mesh.h"

// Ground under an entity's children, in the entity's own space (e.g. a
// region's terrain): animators naming the entity plant their feet on it
struct GroundComponent {
  std::shared_ptr<const GroundMesh> mesh_;
};

#endif  // GROUND_COMPONENT_H

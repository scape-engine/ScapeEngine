#ifndef INSTANCE_SOURCE_COMPONENT_H
#define INSTANCE_SOURCE_COMPONENT_H

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/pcg/edit_layer.h"

// What made each object of the entity's InstancedMeshComponent (an object
// being the instances sharing an anchor): its source and seed. Without an
// InstancedMeshComponent the entity is one object, made as an entity of its
// own: its one source's anchor and matrix are in its parent's space
struct InstanceSourceComponent {
  struct Source {
    glm::vec3 anchor{0.0f};  // as in InstancedMeshComponent::anchors_
    // What it was made from: an object id ("procmodel:<descriptor>", a model
    // path), relative to the project root
    std::string id;
    uint64_t seed = 0;
    // Which point of its cell it is, for edits (PCG::EditLayer)
    PCG::InstanceKey key;
    // Where it is, as its stage's edits place it (PCG::InstanceEdit): in the
    // entity's space, or for a building in its planet's
    glm::mat4 matrix{1.0f};
    // The generator's key for the cell it was placed in, in its stage
    uint64_t cell = 0;
  };
  std::vector<Source> sources_;
  // The generator's stage that placed them; empty: no edits apply to them
  std::string stage_;

  // The object anchored at `anchor`; null: none
  const Source* Find(const glm::vec3& anchor) const {
    for (const Source& source : sources_)
      if (source.anchor == anchor)
        return &source;
    return nullptr;
  }
};

#endif  // INSTANCE_SOURCE_COMPONENT_H

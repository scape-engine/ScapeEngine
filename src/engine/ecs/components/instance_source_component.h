#ifndef INSTANCE_SOURCE_COMPONENT_H
#define INSTANCE_SOURCE_COMPONENT_H

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// What made each object of the entity's InstancedMeshComponent (an object
// being the instances sharing an anchor): its source and seed
struct InstanceSourceComponent {
  struct Source {
    glm::vec3 anchor{0.0f};  // as in InstancedMeshComponent::anchors_
    // What it was made from: an object id ("procmodel:<descriptor>", a model
    // path), relative to the project root
    std::string id;
    uint64_t seed = 0;
  };
  std::vector<Source> sources_;

  // The object anchored at `anchor`; null: none
  const Source* Find(const glm::vec3& anchor) const {
    for (const Source& source : sources_)
      if (source.anchor == anchor)
        return &source;
    return nullptr;
  }
};

#endif  // INSTANCE_SOURCE_COMPONENT_H

#ifndef COMPUTE_BUFFER_COMPONENT_H
#define COMPUTE_BUFFER_COMPONENT_H

#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "engine/renderer/compute/compute_buffer_registry.h"

// Data an entity's compute jobs read, uploaded by the compute system when
// dirty and released when the component goes (on_destroy hook). Runtime only:
// not saved, the system that fills them adds them again
struct ComputeBufferComponent {
  struct Buffer {
    std::string name;
    std::vector<glm::vec4> data;
    bool dirty = true;
    ComputeBufferId id = INVALID_COMPUTE_BUFFER;  // kept by the compute system
  };
  std::vector<Buffer> buffers_;

  // Move-only, so a duplicated entity never shares (and releases) the ids
  ComputeBufferComponent() = default;
  ComputeBufferComponent(ComputeBufferComponent&&) = default;
  ComputeBufferComponent& operator=(ComputeBufferComponent&&) = default;
  ComputeBufferComponent(const ComputeBufferComponent&) = delete;
  ComputeBufferComponent& operator=(const ComputeBufferComponent&) = delete;
};

#endif  // COMPUTE_BUFFER_COMPONENT_H

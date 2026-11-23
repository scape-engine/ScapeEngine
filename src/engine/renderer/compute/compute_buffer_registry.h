#ifndef COMPUTE_BUFFER_REGISTRY_H
#define COMPUTE_BUFFER_REGISTRY_H

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

using ComputeBufferId = uint32_t;
constexpr ComputeBufferId INVALID_COMPUTE_BUFFER = 0;

// GPU buffers of vec4s that compute shaders read, by id. Owned by whoever
// made them: Release them when done
class ComputeBufferRegistry {
public:
  static ComputeBufferRegistry& Instance();

  // INVALID_COMPUTE_BUFFER if the buffer can't be made
  ComputeBufferId Create(uint32_t vec4_count);
  // Uploads the data, growing the buffer when it is bigger
  void Update(ComputeBufferId id, const std::vector<glm::vec4>& data);
  void Release(ComputeBufferId id);

  // GPU handle, invalid for unknown ids
  bgfx::DynamicVertexBufferHandle Get(ComputeBufferId id) const;

  void Clear();

private:
  ComputeBufferRegistry() = default;

  mutable std::mutex mutex_;
  std::unordered_map<ComputeBufferId, bgfx::DynamicVertexBufferHandle>
      buffers_;
  ComputeBufferId next_id_{1};
};

#endif  // COMPUTE_BUFFER_REGISTRY_H

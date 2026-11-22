#ifndef MESH_CACHE_H
#define MESH_CACHE_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/animation/skeleton.h"
#include "engine/core/types/geometry_data.h"

namespace Content {

class MeshCache {
public:
  static MeshCache& Instance();

  // Load with caching; safe from any thread (a worker can load ahead, and
  // entries stay where they are)
  const std::vector<Geometry::MeshData>& Get(const std::string& path);
  // An animated model's skeleton and clips (after Get); null for the rest
  const SkeletalData* Skeletal(const std::string& path) const;

  // Cache management
  void Evict(const std::string& path);
  void Clear();
  size_t Size() const {
    std::lock_guard lock(mutex_);
    return cache_.size();
  }

private:
  MeshCache() = default;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::vector<Geometry::MeshData>> cache_;
  std::unordered_map<std::string, SkeletalData> skeletal_;
};

}  // namespace Content

#endif  // MESH_CACHE_H
#include "mesh_cache.h"

#include "engine/content/loaders/mesh_loader.h"
#include "engine/core/logger.h"

namespace Content {

MeshCache& MeshCache::Instance() {
  static MeshCache instance;
  return instance;
}

const std::vector<Geometry::MeshData>& MeshCache::Get(const std::string& path) {
  {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(path);
    if (it != cache_.end())
      return it->second;
  }

  // Loaded unlocked; a load racing it for the same path keeps the first
  SkeletalData skeletal;
  auto mesh_data = MeshLoader::Load(path, &skeletal);
  if (mesh_data.empty())
    Logger::getInstance().Log(LogLevel::Warning,
                              "[MeshCache] No mesh_data loaded from: " + path);
  std::lock_guard lock(mutex_);
  if (!skeletal.Empty())
    skeletal_.try_emplace(path, std::move(skeletal));
  // An empty entry avoids repeated load attempts
  return cache_.try_emplace(path, std::move(mesh_data)).first->second;
}

const SkeletalData* MeshCache::Skeletal(const std::string& path) const {
  std::lock_guard lock(mutex_);
  auto it = skeletal_.find(path);
  return it == skeletal_.end() ? nullptr : &it->second;
}

void MeshCache::Evict(const std::string& path) {
  std::lock_guard lock(mutex_);
  cache_.erase(path);
  skeletal_.erase(path);
}

void MeshCache::Clear() {
  std::lock_guard lock(mutex_);
  cache_.clear();
  skeletal_.clear();
}

}  // namespace Content
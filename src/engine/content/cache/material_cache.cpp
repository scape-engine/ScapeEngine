#include "material_cache.h"

#include "engine/content/loaders/material_loader.h"
#include "engine/core/logger.h"

namespace Content {

MaterialCache& MaterialCache::Instance() {
  static MaterialCache instance;
  return instance;
}

const std::vector<MaterialData>& MaterialCache::Get(const std::string& path) {
  {
    std::lock_guard lock(mutex_);
    auto it = cache_.find(path);
    if (it != cache_.end())
      return it->second;
  }

  // Loaded unlocked; a load racing it for the same path keeps the first
  auto materials = MaterialLoader::LoadFromScene(path);
  if (materials.empty())
    Logger::getInstance().Log(
        LogLevel::Warning, "[MaterialCache] No materials loaded from: " + path);
  std::lock_guard lock(mutex_);
  return cache_.try_emplace(path, std::move(materials)).first->second;
}

void MaterialCache::Evict(const std::string& path) {
  std::lock_guard lock(mutex_);
  cache_.erase(path);
}

void MaterialCache::Clear() {
  std::lock_guard lock(mutex_);
  cache_.clear();
}

}  // namespace Content
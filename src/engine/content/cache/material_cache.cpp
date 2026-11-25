#include "material_cache.h"

#include "engine/content/loaders/material_loader.h"
#include "engine/core/logger.h"

namespace Content {

MaterialCache& MaterialCache::Instance() {
  static MaterialCache instance;
  return instance;
}

const std::vector<MaterialData>& MaterialCache::Get(const std::string& path) {
  auto it = cache_.find(path);
  if (it != cache_.end())
    return it->second;

  auto materials = MaterialLoader::LoadFromScene(path);
  if (materials.empty()) {
    Logger::getInstance().Log(
        LogLevel::Warning, "[MaterialCache] No materials loaded from: " + path);
    cache_[path] = {};
    return cache_[path];
  }

  auto [emplace_it, _] = cache_.emplace(path, std::move(materials));
  return emplace_it->second;
}

void MaterialCache::Put(const std::string& path,
                        std::vector<MaterialData> materials) {
  cache_[path] = std::move(materials);
}

void MaterialCache::Evict(const std::string& path) {
  cache_.erase(path);
}

void MaterialCache::Clear() {
  cache_.clear();
}

}  // namespace Content
#ifndef MATERIAL_CACHE_H
#define MATERIAL_CACHE_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/types/material_data.h"

namespace Content {

class MaterialCache {
public:
  static MaterialCache& Instance();

  // Safe from any thread, as MeshCache::Get
  const std::vector<MaterialData>& Get(const std::string& path);
  void Evict(const std::string& path);
  void Clear();

private:
  MaterialCache() = default;
  std::mutex mutex_;
  std::unordered_map<std::string, std::vector<MaterialData>> cache_;
};

}  // namespace Content

#endif  // MATERIAL_CACHE_H
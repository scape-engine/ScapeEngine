#ifndef MATERIAL_CACHE_H
#define MATERIAL_CACHE_H

#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/types/material_data.h"

namespace Content {

class MaterialCache {
public:
  static MaterialCache& Instance();

  const std::vector<MaterialData>& Get(const std::string& path);

  // Store materials extracted elsewhere (MeshCache imports both at once)
  void Put(const std::string& path, std::vector<MaterialData> materials);
  void Evict(const std::string& path);
  void Clear();

private:
  MaterialCache() = default;
  std::unordered_map<std::string, std::vector<MaterialData>> cache_;
};

}  // namespace Content

#endif  // MATERIAL_CACHE_H
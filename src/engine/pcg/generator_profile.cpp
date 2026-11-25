#include "generator_profile.h"

#include <fstream>

namespace PCG {

nlohmann::json LoadProfile(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file)
    return nullptr;
  nlohmann::json settings = nlohmann::json::parse(file, nullptr, false);
  return settings.is_discarded() ? nlohmann::json() : settings;
}

bool SaveProfile(const std::filesystem::path& path,
                 const nlohmann::json& settings) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path);
  if (!file)
    return false;
  file << settings.dump(2);
  return static_cast<bool>(file);
}

nlohmann::json ProfileOverrides(const nlohmann::json& profile,
                                const nlohmann::json& settings) {
  if (!profile.is_object() || !settings.is_object())
    return settings;
  nlohmann::json overrides = nlohmann::json::object();
  for (const auto& [key, value] : settings.items()) {
    auto it = profile.find(key);
    if (it == profile.end()) {
      overrides[key] = value;
    } else if (*it != value) {
      overrides[key] = value.is_object() && it->is_object()
                           ? ProfileOverrides(*it, value)
                           : value;
    }
  }
  return overrides;
}

}  // namespace PCG

#include "spawner_generator.h"

namespace PCG {

SpawnerGenerator::SpawnerGenerator() : source(std::make_unique<GridSource>()) {}

SpawnerGenerator::SpawnerGenerator(const SpawnerGenerator& other)
    : source(other.source->Clone()),
      template_profile(other.template_profile),
      bindings(other.bindings),
      edits_folder(other.edits_folder),
      activation_distance(other.activation_distance),
      max_active(other.max_active),
      error(other.error),
      points(other.points),
      spawned(other.spawned),
      build_ms(other.build_ms) {}

nlohmann::json SpawnSettings(
    const nlohmann::json& profile, const Point& point,
    const std::vector<SpawnerGenerator::Binding>& bindings) {
  nlohmann::json settings = profile;
  for (const SpawnerGenerator::Binding& binding : bindings) {
    const auto value = point.attributes.find(binding.attribute);
    if (value == point.attributes.end() || binding.setting.empty() ||
        binding.setting[0] != '/')
      continue;
    nlohmann::json set = *value;
    if (binding.table.is_object()) {
      const std::string key =
          value->is_string() ? value->get<std::string>() : value->dump();
      const auto entry = binding.table.find(key);
      if (entry == binding.table.end())
        continue;
      set = *entry;
    }
    settings[nlohmann::json::json_pointer(binding.setting)] = std::move(set);
  }
  return settings;
}

}  // namespace PCG

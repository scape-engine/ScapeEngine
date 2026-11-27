#include "star_system_source.h"

#include <fstream>
#include <sstream>

#include "content_json.h"

namespace ProcWorld {

nlohmann::json StarSystemSource::Write() const {
  return {{"type", Type()},
          {"content", content_path},
          {"address",
           {{"x", address.x},
            {"y", address.y},
            {"z", address.z},
            {"galaxy", address.galaxy},
            {"index", address.index}}}};
}

void StarSystemSource::Read(const nlohmann::json& in) {
  content_path = in.value("content", content_path);
  if (const auto it = in.find("address"); it != in.end()) {
    address.x = it->value("x", address.x);
    address.y = it->value("y", address.y);
    address.z = it->value("z", address.z);
    address.galaxy = it->value("galaxy", address.galaxy);
    address.index = it->value("index", address.index);
  }
}

bool StarSystemSource::Build(const std::filesystem::path& root,
                             std::vector<PCG::Point>& points,
                             std::string& error) const {
  std::ifstream file(root / content_path);
  if (content_path.empty() || !file) {
    error = "Can't open " + (root / content_path).string();
    return false;
  }
  std::stringstream text;
  text << file.rdbuf();
  Worldgen::SystemContent content;
  if (!Worldgen::Json::LoadSystemContent(text.str(), content, &error))
    return false;

  const Worldgen::StarSystem system = Worldgen::BuildSystem(content, address);
  for (size_t slot = 0; slot < system.planets.size(); ++slot) {
    const Worldgen::SystemPlanet& planet = system.planets[slot];
    PCG::Point& point = points.emplace_back();
    point.name = (planet.parent < 0 ? "Planet " : "Moon ") +
                 std::to_string(slot + 1);
    if (slot < system.orbits.positions.size()) {
      const Worldgen::OrbitPoint& at = system.orbits.positions[slot];
      point.position = {at.x, at.y, at.z};
    }
    nlohmann::json& attributes = point.attributes;
    attributes["seed"] = planet.seed;
    attributes["size"] = planet.size;
    attributes["parent"] = planet.parent;
    attributes["radius"] = slot < system.orbits.radius.size()
                               ? system.orbits.radius[slot]
                               : 0.0f;
    attributes["rings"] = slot < system.orbits.rings.size() &&
                          system.orbits.rings[slot];
    if (slot < system.biomes.size()) {
      attributes["biome"] = system.biomes[slot].biome;
      attributes["subtype"] = system.biomes[slot].subtype;
      attributes["force_continents"] = system.biomes[slot].force_continents;
    }
    attributes["star_type"] = system.star.type;
    attributes["race"] = system.star.race;
  }
  return true;
}

}  // namespace ProcWorld

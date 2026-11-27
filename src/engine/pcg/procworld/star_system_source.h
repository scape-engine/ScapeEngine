#ifndef STAR_SYSTEM_SOURCE_H
#define STAR_SYSTEM_SOURCE_H

#include <string>

#include "engine/pcg/points/point_source.h"

#include "api/star_system.h"

namespace ProcWorld {

// The star system at an address (worldgen's BuildSystem over the system
// content, JSON, project-relative), one point per planet slot at its orbit
// position, the star at the origin. Each point's attributes: "seed", "size",
// "parent" (a moon's planet slot; -1: a planet), "radius" (its orbit
// layout's), "rings", and, when the star type has biome rules, "biome",
// "subtype" and "force_continents"; the star's "star_type" and "race"
class StarSystemSource : public PCG::PointSource {
public:
  std::string Type() const override { return "StarSystem"; }
  std::unique_ptr<PCG::PointSource> Clone() const override {
    return std::make_unique<StarSystemSource>(*this);
  }
  nlohmann::json Write() const override;
  void Read(const nlohmann::json& in) override;
  bool Build(const std::filesystem::path& root,
             std::vector<PCG::Point>& points,
             std::string& error) const override;

  std::string content_path = "worldgen/system.json";
  Worldgen::SystemAddress address;
};

}  // namespace ProcWorld

#endif  // STAR_SYSTEM_SOURCE_H

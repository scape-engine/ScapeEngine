#include "point_source.h"

namespace PCG {

nlohmann::json GridSource::Write() const {
  return {{"type", Type()},
          {"count", {count.x, count.y}},
          {"spacing", spacing},
          {"seed", seed}};
}

void GridSource::Read(const nlohmann::json& in) {
  if (const auto it = in.find("count"); it != in.end())
    count = {it->at(0).get<int>(), it->at(1).get<int>()};
  spacing = in.value("spacing", spacing);
  seed = in.value("seed", seed);
}

// SplitMix64's finaliser: a point's seed from the source's and its place
static uint64_t _Mix(uint64_t value) {
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

bool GridSource::Build(const std::filesystem::path&,
                       std::vector<Point>& points, std::string& error) const {
  if (count.x <= 0 || count.y <= 0) {
    error = "The grid has no points";
    return false;
  }
  const glm::vec2 centre = 0.5f * glm::vec2(count - 1) * spacing;
  for (int z = 0; z < count.y; ++z)
    for (int x = 0; x < count.x; ++x) {
      Point& point = points.emplace_back();
      point.name = "Point " + std::to_string(x) + "," + std::to_string(z);
      point.position = {x * spacing - centre.x, 0.0f, z * spacing - centre.y};
      const uint64_t place =
          static_cast<uint64_t>(z) << 32 | static_cast<uint32_t>(x);
      point.attributes = {
          {"seed", _Mix(seed ^ _Mix(place))}, {"x", x}, {"z", z}};
    }
  return true;
}

}  // namespace PCG

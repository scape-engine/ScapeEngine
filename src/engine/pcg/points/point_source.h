#ifndef PCG_POINT_SOURCE_H
#define PCG_POINT_SOURCE_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace PCG {

// A point a source makes, as UE's PCG points: where it stands (in its
// spawner's space) and its attributes, named values (numbers, strings,
// booleans) a spawner hands to what it makes there. Its name is unique among
// its source's points and stays the same while the source's settings do
struct Point {
  std::string name;
  glm::vec3 position{0.0f};
  nlohmann::json attributes = nlohmann::json::object();
};

// Makes points from its settings: the star system at an address, a grid, a
// list. A spawner (SpawnerGenerator) makes a generator at each
class PointSource {
public:
  virtual ~PointSource() = default;

  // Its type's name, as its settings store it
  virtual std::string Type() const = 0;
  virtual std::unique_ptr<PointSource> Clone() const = 0;
  virtual nlohmann::json Write() const = 0;
  // Missing values stay as they are
  virtual void Read(const nlohmann::json& in) = 0;

  // Its points; paths are relative to `root`. Any thread. False, with
  // `error`, when it can't make them
  virtual bool Build(const std::filesystem::path& root,
                     std::vector<Point>& points, std::string& error) const = 0;
};

// Points on a grid of `count` across x and z, `spacing` apart, centred on
// the origin; each with a seed from the source's seed and its place
// (attribute "seed") and its column and row ("x", "z")
class GridSource : public PointSource {
public:
  std::string Type() const override { return "Grid"; }
  std::unique_ptr<PointSource> Clone() const override {
    return std::make_unique<GridSource>(*this);
  }
  nlohmann::json Write() const override;
  void Read(const nlohmann::json& in) override;
  bool Build(const std::filesystem::path& root, std::vector<Point>& points,
             std::string& error) const override;

  glm::ivec2 count{3, 3};
  float spacing = 100.0f;
  uint64_t seed = 1;
};

}  // namespace PCG

#endif  // PCG_POINT_SOURCE_H

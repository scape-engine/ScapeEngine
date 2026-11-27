#ifndef PCG_SPAWNER_GENERATOR_H
#define PCG_SPAWNER_GENERATOR_H

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/pcg/generator_base.h"
#include "engine/pcg/points/point_source.h"

namespace PCG {

// Makes a generator at each point of its source, as UE's PCG spawns actors
// at points: each starts from the template profile (a .generator asset) with
// its point's attributes bound into its settings, so a spawned generator is
// the profile plus its point's overrides. They're children of its volume,
// remade with it and never saved; each keeps its hand edits in its own edit
// layer, named after its point.
// A star system's planets are a spawner over the system's points with a
// planet profile; a city's blocks one over a grid with a block's
// TODO: a points graph (as UE's PCG graphs) making the points; Scape's graph
// only evaluates height fields, so the source is a setting for now
class SpawnerGenerator : public Generator {
public:
  SpawnerGenerator();
  SpawnerGenerator(const SpawnerGenerator& other);

  GeneratorType GetType() const override { return GeneratorType::Spawner; }
  std::string GetDisplayName() const override { return "Spawner"; }
  std::unique_ptr<Generator> Clone() const override {
    return std::make_unique<SpawnerGenerator>(*this);
  }

  // Where the points come from; never null
  std::unique_ptr<PointSource> source;
  // The profile each spawned generator starts from, project-relative
  std::string template_profile;
  // A point's attribute set into a setting, by JSON pointer ("/seed",
  // "/stages/herds/enabled"); with a table, the setting takes the table's
  // value for the attribute's (as text: "3"), and stays when it has none
  struct Binding {
    std::string attribute;
    std::string setting;
    nlohmann::json table = nullptr;

    bool operator==(const Binding&) const = default;
  };
  std::vector<Binding> bindings;
  // The folder (project-relative) of the spawned generators' edit layers,
  // one per point, named after it; empty: "edits/<its volume's name>"
  std::string edits_folder;
  // Their ActivationComponent's: a spawned generator works fully only with
  // a viewer this near (0: always), and at most max_active of them at once,
  // the nearest (0: no cap)
  float activation_distance = 0.0f;
  uint32_t max_active = 0;

  // State, written by its spawner system
  std::string error;
  std::vector<Point> points;
  size_t spawned = 0;
  float build_ms = 0.0f;
};

// The settings a generator at `point` gets: the profile with the point's
// attributes bound in (bindings whose attribute or table entry is missing,
// or whose setting isn't a JSON pointer, are skipped)
nlohmann::json SpawnSettings(
    const nlohmann::json& profile, const Point& point,
    const std::vector<SpawnerGenerator::Binding>& bindings);

}  // namespace PCG

#endif  // PCG_SPAWNER_GENERATOR_H

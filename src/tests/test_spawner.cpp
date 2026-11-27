// Checks must run in release builds too
#undef NDEBUG
#include <cassert>

#include <filesystem>
#include <iostream>
#include <set>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/ecs/components/activation_component.h"
#include "engine/ecs/components/transform_component.h"
#include "engine/paging/activation.h"
#include "engine/paging/viewers.h"
#include "engine/pcg/points/spawner_generator.h"
#include "engine/pcg/procworld/generator_settings.h"

namespace Tests {

// A grid's points are named and seeded apart; bindings set a point's
// attributes into a template's settings, through a table when given; a
// spawner's settings read back the same
void TestSpawner() {
  PCG::GridSource grid;
  grid.count = {3, 2};
  grid.spacing = 10.0f;
  std::vector<PCG::Point> points;
  std::string error;
  assert(grid.Build({}, points, error) && points.size() == 6);
  std::set<std::string> names;
  std::set<uint64_t> seeds;
  for (const PCG::Point& point : points) {
    names.insert(point.name);
    seeds.insert(point.attributes.at("seed").get<uint64_t>());
  }
  assert(names.size() == 6 && seeds.size() == 6);
  // Centred on the origin
  assert(points.front().position == glm::vec3(-10.0f, 0.0f, -5.0f));
  assert(points.back().position == glm::vec3(10.0f, 0.0f, 5.0f));

  PCG::Point point;
  point.attributes = {{"seed", 42}, {"biome", 1}, {"size", 2}};
  const nlohmann::json profile = {
      {"seed", 1}, {"planet", "worldgen/planet.json"}, {"stages", {}}};
  const std::vector<PCG::SpawnerGenerator::Binding> bindings = {
      {"seed", "/seed"},
      {"biome", "/planet", {{"1", "worldgen/lush.json"}}},
      {"size", "/stages/herds/grid", {{"5", 3}}},  // not in the table: kept
      {"missing", "/seed"},                        // no such attribute
      {"seed", "seed"}};                           // not a JSON pointer
  const nlohmann::json settings = PCG::SpawnSettings(profile, point, bindings);
  assert(settings.at("seed") == 42);
  assert(settings.at("planet") == "worldgen/lush.json");
  assert(settings.at("stages").empty());

  PCG::SpawnerGenerator spawner;
  spawner.source = ProcWorld::CreatePointSource(
      {{"type", "StarSystem"},
       {"content", "worldgen/system.json"},
       {"address", {{"x", 5}, {"index", 7}}}});
  assert(spawner.source && spawner.source->Type() == "StarSystem");
  spawner.template_profile = "profiles/planet.generator";
  spawner.bindings = bindings;
  const nlohmann::json written = ProcWorld::WriteSettings(spawner);
  const auto read = ProcWorld::CreateGenerator(written);
  assert(read && read->GetType() == PCG::GeneratorType::Spawner);
  const auto& back = static_cast<const PCG::SpawnerGenerator&>(*read);
  assert(back.template_profile == spawner.template_profile);
  assert(back.bindings == spawner.bindings);
  assert(back.source->Write() == spawner.source->Write());

  // The example project's system: a point per planet slot, each with a
  // planet's inputs
  const std::filesystem::path example =
      std::filesystem::path(__FILE__).parent_path() /
      "../../examples/example-project";
  if (std::filesystem::exists(example / "worldgen/system.json")) {
    std::vector<PCG::Point> planets;
    assert(spawner.source->Build(example, planets, error));
    assert(!planets.empty());
    for (const PCG::Point& planet : planets)
      assert(planet.attributes.contains("seed") &&
             planet.attributes.contains("size"));
    std::cout << planets.size() << " planets, the first "
              << planets.front().attributes.dump() << "\n";
  }

  std::cout << "Spawner tests passed\n";
}

// Active within the distance of the extent, staying so a little farther;
// at most max_active of a parent's, the nearest; 0: always
void TestActivation() {
  ECS ecs;
  auto [parent, parent_transform] = ecs.CreateEntity("System");
  const auto place = [&](Entity entity, float x) {
    ecs.Get<TransformComponent>(entity).model_ =
        glm::translate(glm::mat4(1.0f), {x, 0.0f, 0.0f});
  };
  const auto make = [&](float x) {
    auto [entity, transform] = ecs.CreateEntity("Planet", parent);
    place(entity, x);
    auto& activation = ecs.Add<ActivationComponent>(entity);
    activation.distance = 100.0f;
    activation.extent = 50.0f;
    return entity;
  };
  const Entity near = make(120.0f), far = make(500.0f), edge = make(140.0f);
  const auto active = [&](Entity entity) {
    return ecs.Get<ActivationComponent>(entity).active_;
  };
  // The host alone: viewer entities need ECS::Main
  Viewers::SetHost(glm::vec3(0.0f));
  Viewers::Update(ecs);
  Activation::Update(ecs);
  assert(active(near) && active(edge) && !active(far));

  // 105 m past the edge's extent: within the slack only once active
  place(edge, 155.0f);
  Activation::Update(ecs);
  assert(active(edge));
  ecs.Get<ActivationComponent>(edge).active_ = false;
  Activation::Update(ecs);
  assert(!active(edge));

  place(edge, 140.0f);
  ecs.Get<ActivationComponent>(near).max_active = 1;
  ecs.Get<ActivationComponent>(edge).max_active = 1;
  Activation::Update(ecs);
  assert(active(near) && !active(edge));

  ecs.Get<ActivationComponent>(far).distance = 0.0f;
  Activation::Update(ecs);
  assert(active(far));
  Viewers::SetHost(std::nullopt);
  std::cout << "Activation tests passed\n";
}

}  // namespace Tests

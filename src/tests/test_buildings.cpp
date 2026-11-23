// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <set>

#include <nlohmann/json.hpp>

#include "content_json.h"
#include "water/wave_cascades.h"
#include "api/planet_grid.h"
#include "api/region.h"
#include "terrain/polygonizer.h"
#include "engine/pcg/procmodel/descriptor/model_descriptor_parser.h"
#include "engine/pcg/procworld/instancing/building_library.h"

namespace Tests {

static const char* project = "examples/example-project";

// A turned, scaled and moved matrix comes back from its scene transform
static void TestSceneTransform() {
  glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(1, -2, 3));
  m = glm::rotate(m, glm::radians(30.0f), glm::vec3(0, 1, 0));
  m = glm::rotate(m, glm::radians(-50.0f), glm::vec3(1, 0, 0));
  m = glm::rotate(m, glm::radians(70.0f), glm::vec3(0, 0, 1));
  m = glm::scale(m, glm::vec3(2, 0.5f, 3));
  float rows[4][3];
  Worldgen::SceneMatrix(ProcWorld::SceneTransformOf(m), rows);
  for (int r = 0; r < 4; ++r)
    for (int k = 0; k < 3; ++k)
      assert(std::fabs(rows[r][k] - m[r][k]) < 1e-4f);
}

// The example glbs as scenes: a module's part locators, a tower level's mesh
// and the locators levels snap on
static void TestGlbScenes() {
  const auto room = ProcWorld::LoadGlbScene(
      std::string(project) + "/models/buildings/room_door.glb");
  assert(room && room->children.size() == 6);
  for (const Worldgen::SceneNode& n : room->children)
    assert(n.kind == Worldgen::SceneNodeKind::Locator);
  assert(room->children[0].name == "FLOOR_1");

  const auto level = ProcWorld::LoadGlbScene(
      std::string(project) + "/models/buildings/tower_mid.glb");
  assert(level && level->children.size() == 3);
  assert(level->children[0].kind == Worldgen::SceneNodeKind::Mesh);
  assert(level->children[0].attributes.size() == 6);
  assert(level->children[2].name == "LEVEL_OUT_1" &&
         level->children[2].transform.translation[1] == 3.0f);
  assert(!ProcWorld::LoadGlbScene("missing.glb"));
}

// The example planet's styles: outposts solve for most seeds, from the kit's
// parts; towers grow from their base
static void TestExampleStyles() {
  std::ifstream file(std::string(project) + "/worldgen/planet.json");
  std::stringstream text;
  text << file.rdbuf();
  Worldgen::PlanetContent planet;
  assert(Worldgen::Json::LoadPlanetContent(text.str(), planet));
  assert(planet.building_styles.size() == 3);
  ProcWorld::BuildingLibrary library(planet.building_styles, project);
  int solved = 0;
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto pieces = library.Structure(0, seed);
    assert(pieces == library.Structure(0, seed));  // kept
    solved += !pieces->empty();
    for (const Worldgen::BuildingPiece& piece : *pieces)
      assert(piece.model.rfind("models/buildings/", 0) == 0);
  }
  assert(solved >= 15);
  const auto tower = library.Structure(1, 5);
  assert(tower->size() >= 2 &&
         tower->front().model == "models/buildings/tower_base.glb");
  assert(library.Structure(7, 5)->empty());
  std::cout << "Buildings: OK (" << solved << "/20 outposts)" << std::endl;
}

// The example craft: a fleet of six weighted craft and a patrol, each
// taking a rover or a skiff, whose descriptors parse
static void TestExampleCraft() {
  std::ifstream file(std::string(project) + "/worldgen/craft.json");
  std::stringstream text;
  text << file.rdbuf();
  Worldgen::CraftContent content;
  assert(Worldgen::Json::LoadCraftContent(text.str(), content));
  std::set<std::string> models;
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto fleet = Worldgen::BuildFleet(content, seed, false);
    assert(fleet.size() == 7 && fleet.back().slot.hint == "PATROL");
    for (const Worldgen::FleetEntry& entry : fleet)
      models.insert(Worldgen::BuildCraft(content, entry.slot, entry.seed).model);
  }
  assert(models == std::set<std::string>({"procmodel:models/craft/rover.json",
                                          "procmodel:models/craft/skiff.json"}));
  for (const std::string& model : models) {
    const std::filesystem::path path =
        std::filesystem::path(project) / model.substr(model.find(':') + 1);
    std::ifstream descriptor(path);
    ProcModel::ModelDescriptor d;
    assert(ProcModel::ModelDescriptorParser::FromJson(
        nlohmann::json::parse(descriptor), d));
    assert(std::filesystem::exists(path.parent_path() / d.path));
  }
  std::cout << "Craft: OK (" << models.size() << " models)" << std::endl;
}

// The example sea: the region terrain has a sea level and the planet a wave
// list per water state, rougher state by state, each split into cascades
static void TestExampleSea() {
  const auto read = [](const std::string& name) {
    std::ifstream file(std::string(project) + "/worldgen/" + name);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
  };
  Worldgen::RegionContent region;
  assert(Worldgen::Json::LoadRegionContent(read("region.json"), region));
  assert(region.terrain.sea_level > 0.0f);
  // A good share of the ground lies under it, so it shows: terrain heights
  // over a sample of coarse regions around the planet
  {
    const int level = 3;
    const int side = Worldgen::PlanetGrid::CellsPerSide(region.radius, level);
    std::vector<float> heights;
    for (int face = 0; face < 6; ++face)
      for (int i = 0; i < side; i += 2)
        for (int j = 0; j < side; j += 2) {
          int centre[3];
          Worldgen::PlanetGrid::CellCentre({face, i, j, level}, region.radius,
                                           centre);
          const Worldgen::Region built = Worldgen::BuildRegion(
              region, centre[0], centre[1], centre[2], level);
          for (const auto& vertex : built.mesh) {
            float p[3], n[3];
            Worldgen::Polygonizer::DecodeVertex(vertex, p, n);
            const float x = p[0] + built.origin[0], y = p[1] + built.origin[1],
                        z = p[2] + built.origin[2];
            heights.push_back(std::sqrt(x * x + y * y + z * z) - region.radius);
          }
        }
    std::sort(heights.begin(), heights.end());
    size_t under = 0;
    while (under < heights.size() && heights[under] < region.terrain.sea_level)
      ++under;
    const double share = double(under) / double(heights.size());
    assert(share > 0.2 && share < 0.8);
    std::cout << "Sea: " << int(share * 100.0) << "% of the ground under it"
              << std::endl;
  }
  Worldgen::PlanetContent planet;
  assert(Worldgen::Json::LoadPlanetContent(read("planet.json"), planet));
  const Worldgen::WaterWaveSettings& waves = planet.water_waves;
  assert(waves.states.size() == planet.water_states.weights[0].size());

  float previous = 0.0f;
  std::string sums;
  for (int state = 0; state < int(waves.states.size()); ++state) {
    Worldgen::WaveField field;
    Worldgen::SetWaterState(field, state, 0.0f);
    Worldgen::UpdateWaves(waves, 0.1f, field);
    assert(!field.waves.empty() && field.wave_sum > previous);
    previous = field.wave_sum;
    sums += " " + std::to_string(field.wave_sum).substr(0, 5);

    std::vector<Worldgen::Wave> sorted = field.waves;
    Worldgen::SortWaves(sorted);
    const Worldgen::WaveCascadeSettings settings;
    const Worldgen::WaveCascades cascades =
        Worldgen::BuildWaveCascades(sorted, settings, Worldgen::WaveFrame());
    assert(!cascades.groups.empty());
    assert(int(cascades.starts.size()) == settings.count + 1);
    for (int c = 0; c < settings.count; ++c)
      assert(cascades.starts[c] <= cascades.starts[c + 1]);
    assert(cascades.starts.back() == int(cascades.groups.size()));
  }
  // A storm stays a sea, not a mountain range
  assert(previous < 10.0f);
  std::cout << "Sea: OK (wave sums" << sums << ")" << std::endl;
}

void TestBuildings() {
  TestSceneTransform();
  TestGlbScenes();
  TestExampleStyles();
  TestExampleCraft();
  TestExampleSea();
}

}  // namespace Tests

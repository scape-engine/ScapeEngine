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

void TestBuildings() {
  TestSceneTransform();
  TestGlbScenes();
  TestExampleStyles();
  TestExampleCraft();
}

}  // namespace Tests

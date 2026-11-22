#ifndef BUILDING_LIBRARY_H
#define BUILDING_LIBRARY_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "api/building.h"
#include "structures/scene_node.h"

namespace ProcWorld {

// A glb's node tree as worldgen's scenes: nodes with meshes are Mesh nodes
// (with their bounds), the rest Locators; transforms keep their rotation,
// scale and translation. Null for a file that doesn't load
std::unique_ptr<Worldgen::SceneNode> LoadGlbScene(
    const std::filesystem::path& path);

// A local matrix as a scene transform (SceneMatrix gives it back)
Worldgen::SceneTransform SceneTransformOf(const glm::mat4& local);

// A planet's buildings: the style of each building class and the structures
// made from them, kept by class and seed. Scenes and models are glb paths
// relative to the root. Safe to use from the region jobs.
// L-system scenes attach parts at top-level locators whose names hold the
// rule's locator type and the style's in or out marker. At most
// max_structures structures are kept; scenes stay until the planet reloads
// (one set per style)
class BuildingLibrary {
public:
  BuildingLibrary(std::vector<Worldgen::BuildingStyle> styles,
                  std::filesystem::path root);

  // The pieces of a building of class `cls` with `seed`; none for a class
  // without a style
  std::shared_ptr<const std::vector<Worldgen::BuildingPiece>> Structure(
      int cls, uint64_t seed);

  static constexpr size_t max_structures = 4096;

private:
  const Worldgen::SceneNode* Scene(const std::string& file);

  std::vector<Worldgen::BuildingStyle> styles_;
  std::filesystem::path root_;
  Worldgen::LSystem::Files files_;
  std::mutex scenes_mutex_, structures_mutex_;
  std::map<std::string, std::unique_ptr<Worldgen::SceneNode>> scenes_;
  std::map<std::pair<int, uint64_t>,
           std::shared_ptr<const std::vector<Worldgen::BuildingPiece>>>
      structures_;
};

}  // namespace ProcWorld

#endif  // BUILDING_LIBRARY_H

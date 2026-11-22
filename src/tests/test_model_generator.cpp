// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/content/loaders/mesh_loader.h"
#include "engine/pcg/procmodel/descriptor/model_descriptor_parser.h"
#include "engine/pcg/procmodel/generator/descriptor_resolver.h"
#include "engine/pcg/procmodel/generator/model_generator.h"
#include "engine/pcg/procmodel/model_graph/model_graph_builder.h"

#include "content/model_parts.h"

namespace Tests {

static ProcModel::ModelGraphNode _Node(const std::string& name,
                                       const glm::vec3& at) {
  ProcModel::ModelGraphNode node;
  node.name = name;
  node.local_transform = glm::translate(glm::mat4(1.0f), at);
  return node;
}

static void _Index(ProcModel::ModelGraph& graph, ProcModel::ModelGraphNode& node,
                   const glm::mat4& parent) {
  node.world_transform = parent * node.local_transform;
  graph.node_lookup[node.name] = &node;
  for (auto& child : node.children)
    _Index(graph, child, node.world_transform);
}

static int _Depth(const std::string& id) {
  int depth = 0;
  for (size_t at = id.find("_under_"); at != std::string::npos;
       at = id.find("_under_", at + 1))
    ++depth;
  return depth;
}

// A trunk with two locators, branches on them (one or two kept) and twigs on
// each branch's locator, twigs on twigs, down to max_depth
void TestModelGenerator() {
  ProcModel::ModelGraph graph;
  graph.root = _Node("ROOT", glm::vec3(0.0f));
  auto trunk = _Node("TRUNK", glm::vec3(0.0f));
  trunk.children = {_Node("L1", glm::vec3(0, 2, 0)),
                    _Node("L2", glm::vec3(1, 2, 0))};
  auto branch = _Node("BRANCH", glm::vec3(5, 0, 0));
  branch.children = {_Node("B1", glm::vec3(0, 1, 0))};
  graph.root.children = {trunk, branch};
  _Index(graph, graph.root, glm::mat4(1.0f));

  ProcModel::ModelDescriptor d;
  d.model_id = "test";
  d.max_depth = 4;
  ProcModel::SelectionGroup trunks;
  trunks.group_id = "trunk";
  trunks.parts = {{"TRUNK", "TRUNK", 1.0f}, {"NOT_IN_GRAPH", "", 0.0f}};
  ProcModel::SelectionGroup branches;
  branches.group_id = "branches";
  branches.parent = {"TRUNK"};
  branches.locators = std::vector<std::string>{"L1", "L2"};
  branches.unique_per_locator = true;
  branches.keep = glm::ivec2(1, 2);
  branches.parts = {{"BRANCH", "BRANCH", 1.0f}};
  ProcModel::SelectionGroup twigs;
  twigs.group_id = "twigs";
  twigs.parent = {"BRANCH"};
  twigs.locators = std::vector<std::string>{"B1"};
  twigs.scale_factor = 0.5f;
  twigs.parts = {{"BRANCH", "BRANCH", 1.0f}};
  d.selection_groups = {trunks, branches, twigs};

  PCG::LinearPipeline pipeline;
  PCG::OperationRegistry registry;
  auto generate = [&](uint64_t seed) {
    auto out = ProcModel::ModelGenerator::Generate(graph, d, pipeline,
                                                   registry, seed, 1);
    assert(out);  // a weight 0 part is never picked
    return out->model.descriptors;
  };

  std::set<size_t> counts;
  for (uint64_t seed = 1; seed <= 50; ++seed) {
    const auto parts = generate(seed);
    // the same seed, the same model
    const auto again = generate(seed);
    assert(parts.size() == again.size());
    for (size_t i = 0; i < parts.size(); ++i)
      assert(parts[i].descriptor_id == again[i].descriptor_id &&
             parts[i].local_transform == again[i].local_transform);

    // a trunk, then per kept branch the branch and two levels of twigs
    assert(parts.size() == 4 || parts.size() == 7);
    counts.insert(parts.size());
    for (const auto& part : parts) {
      assert(_Depth(part.descriptor_id) < d.max_depth);
      // the deepest twig sits on the twig below it: its parent's sampled
      // scale (0.5) carries into its place and size
      if (_Depth(part.descriptor_id) == 3 &&
          part.descriptor_id.find("_at_L1_") != std::string::npos) {
        const glm::vec3 at(part.local_transform[3]);
        assert(glm::length(at - glm::vec3(0.0f, 3.5f, 0.0f)) < 1e-5f);
        assert(std::abs(glm::length(glm::vec3(part.local_transform[0])) -
                        0.5f) < 1e-5f);
        assert(part.applied_scale == glm::vec3(0.5f));
      }
    }
  }
  assert(counts.size() == 2);  // both one and two branches are kept

  // Two lists under the trunk pick as worldgen picks a part's two child
  // lists, each from its own seed
  for (const char* name : {"A1", "A2", "B1", "B2"})
    graph.root.children.push_back(_Node(name, glm::vec3(0.0f)));
  graph.node_lookup.clear();
  _Index(graph, graph.root, glm::mat4(1.0f));
  ProcModel::ModelDescriptor listed;
  listed.model_id = "lists";
  ProcModel::SelectionGroup a, b;
  a.group_id = "a";
  a.parent = {"TRUNK"};
  a.parts = {{"A1", "A1", 1.0f}, {"A2", "A2", 1.0f}};
  b.group_id = "b";
  b.parent = {"TRUNK"};
  b.list = 1;
  b.parts = {{"B1", "B1", 1.0f}, {"B2", "B2", 1.0f}};
  listed.selection_groups = {trunks, a, b};
  listed.selection_groups[0].parts.pop_back();

  Worldgen::PartList root;
  Worldgen::Part trunk_part{"TR", 1000};
  for (const std::string side : {"A", "B"}) {
    Worldgen::PartList child;
    child.types.push_back({false, {{side + "1", 1000}, {side + "2", 1000}}});
    trunk_part.children.push_back(child);
  }
  root.types.push_back({false, {trunk_part}});
  const Worldgen::PartLoader load = [&root](const char*) { return &root; };

  std::set<std::string> pairs;
  for (uint64_t seed = 1; seed <= 50; ++seed) {
    auto out = ProcModel::ModelGenerator::Generate(graph, listed, pipeline,
                                                   registry, seed, 1);
    assert(out);
    std::set<std::string> got;
    for (const auto& part : out->model.descriptors)
      got.insert(part.descriptor_id.substr(0, 2));
    const auto want = Worldgen::PickPartsSingle(load, "", seed, true);
    assert(got == std::set<std::string>(want.begin(), want.end()) &&
           got.size() == 3);
    pairs.insert(*got.begin() + *std::next(got.begin()));
  }
  assert(pairs.size() == 4);  // the lists pick apart

  std::cout << "Model generator: OK" << std::endl;
}

// tests picks <descriptor> <seed in hex>...: per seed a line of the picked
// part ids, each followed by a space
int PrintPicks(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: tests picks <descriptor> <seed in hex>..."
              << std::endl;
    return 1;
  }
  const std::filesystem::path path = argv[0];
  ProcModel::ModelDescriptor d;
  std::ifstream file(path);
  if (!file ||
      !ProcModel::ModelDescriptorParser::FromJson(nlohmann::json::parse(file),
                                                  d))
    return 1;
  const Content::SceneData scene =
      Content::MeshLoader::LoadScene((path.parent_path() / d.path).string());
  ProcModel::ModelGraph graph =
      ProcModel::ModelGraphBuilder::Build(scene, d.path);
  if (!ProcModel::DescriptorResolver::Resolve(graph, d).success)
    return 1;
  std::unordered_map<std::string, const ProcModel::SelectionGroup*> groups;
  for (const auto& g : d.selection_groups)
    groups[g.group_id] = &g;
  PCG::LinearPipeline pipeline;
  PCG::OperationRegistry registry;
  for (int i = 1; i < argc; ++i) {
    const auto out = ProcModel::ModelGenerator::Generate(
        graph, d, pipeline, registry, std::stoull(argv[i], nullptr, 16), 1);
    if (!out) {
      std::cout << "failed" << std::endl;
      continue;
    }
    // a placed part's id starts its descriptor id
    for (const auto& placed : out->model.descriptors)
      for (const auto& part : groups.at(placed.group_id)->parts) {
        const std::string& id = placed.descriptor_id;
        const size_t n = part.id.size();
        if (id.compare(0, n, part.id) == 0 &&
            (id.size() == n || id.compare(n, 4, "_at_") == 0 ||
             id.compare(n, 7, "_under_") == 0)) {
          std::cout << part.id << ' ';
          break;
        }
      }
    std::cout << std::endl;
  }
  return 0;
}

}  // namespace Tests

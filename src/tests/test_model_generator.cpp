// Checks must run in release builds too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>

#include <glm/gtc/matrix_transform.hpp>

#include "engine/pcg/procmodel/generator/model_generator.h"

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

  std::cout << "Model generator: OK" << std::endl;
}

}  // namespace Tests

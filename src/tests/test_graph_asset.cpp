// Checks must run in release builds too
#undef NDEBUG
#include <cassert>

#include <filesystem>
#include <iostream>

#include "engine/pcg/graph/generator_graph.h"
#include "engine/pcg/graph/graph_serializer.h"
#include "engine/pcg/graph/node_types.h"
#include "engine/pcg/procworld/generator_settings.h"

namespace Tests {

// A node as the PCG graph panel adds one: its ports and defaults
static PCG::ProgramGraph::Node& _Add(PCG::ProgramGraph& graph,
                                     PCG::NodeTypeID id) {
  const auto type_id = static_cast<uint32_t>(id);
  const PCG::NodeType& type = PCG::NodeTypeDB::Instance().Get(type_id);
  PCG::ProgramGraph::Node& node = *graph.CreateNode(type_id);
  node.inputs.resize(type.inputs.size());
  node.outputs.resize(type.outputs.size());
  node.default_inputs.resize(type.inputs.size());
  node.params.resize(type.params.size());
  for (size_t i = 0; i < type.inputs.size(); ++i)
    node.default_inputs[i] = type.inputs[i].default_value;
  for (size_t i = 0; i < type.params.size(); ++i)
    node.params[i] = type.params[i].default_value;
  return node;
}

// A graph asset keeps its nodes, places, values and links; a graph
// generator names it in its settings and reads it back
void TestGraphAsset() {
  PCG::ProgramGraph graph;
  PCG::ProgramGraph::Node& a = _Add(graph, PCG::NodeTypeID::OpenSimplex2_Deriv);
  PCG::ProgramGraph::Node& b = _Add(graph, PCG::NodeTypeID::OpenSimplex2_Deriv);
  a.gui_position = glm::vec2(10.0f, 20.0f);
  a.params[0] = 7;
  b.default_inputs[1] = 3.5f;
  assert(graph.CanConnect({a.id, 0}, {b.id, 0}));
  graph.Connect({a.id, 0}, {b.id, 0});

  const std::filesystem::path file =
      std::filesystem::temp_directory_path() / "test_graph_asset.pcggraph";
  assert(PCG::GraphSerializer::SaveToFile(graph, file.string()));

  PCG::GeneratorGraph generator;
  generator.graph_path = "graphs/test.pcggraph";
  assert(generator.Load(file.string()));
  const PCG::ProgramGraph& loaded = *generator.GetGraph();
  assert(loaded.GetNodesCount() == 2);
  const PCG::ProgramGraph::Node& la = loaded.GetNode(a.id);
  const PCG::ProgramGraph::Node& lb = loaded.GetNode(b.id);
  assert(la.gui_position == glm::vec2(10.0f, 20.0f));
  assert(std::get<int>(la.params[0]) == 7);
  assert(lb.default_inputs[1] == 3.5f);
  assert(loaded.IsConnected({a.id, 0}, {b.id, 0}));
  // New nodes don't take a loaded node's id
  PCG::ProgramGraph copy;
  copy.CopyFrom(loaded);
  assert(copy.GenerateNodeId() > std::max(a.id, b.id));

  // A clone owns its own copy of the graph
  {
    auto clone = generator.Clone();
    assert(clone->GetGraph()->GetNodesCount() == 2);
  }
  assert(generator.GetGraph()->GetNodesCount() == 2);

  const nlohmann::json settings = ProcWorld::WriteSettings(generator);
  assert(settings["type"] == "Graph" &&
         settings["graph"] == "graphs/test.pcggraph");
  auto made = ProcWorld::CreateGenerator(settings);
  assert(made && made->GetType() == PCG::GeneratorType::Graph);
  assert(static_cast<PCG::GeneratorGraph&>(*made).graph_path ==
         "graphs/test.pcggraph");

  assert(!generator.Load((file.string() + ".missing")));
  assert(generator.GetGraph()->GetNodesCount() == 0);
  std::filesystem::remove(file);

  std::cout << "Graph asset tests passed\n";
}

}  // namespace Tests

// Checks must run in release builds too
#undef NDEBUG
#include <cassert>

#include <cmath>
#include <filesystem>
#include <iostream>

#include "engine/pcg/graph/generator_graph.h"
#include "engine/pcg/graph/graph_volumes.h"
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

// The height a graph makes at `at`, or NaN if it doesn't compile
static float _Height(const PCG::ProgramGraph& graph, glm::vec2 at) {
  PCG::GraphRuntime runtime;
  if (!runtime.Compile(graph).success)
    return std::nanf("");
  PCG::GraphRuntime::State state;
  runtime.PrepareState(state, 1);
  PCG::Sample sample;
  runtime.GenerateSingle(state, at, sample);
  return sample.height;
}

// The input, output and math nodes compile and run, folded or not
void TestGraphNodes() {
  using ID = PCG::NodeTypeID;
  {
    // y * 2 + x, Y reached first: inputs bind by type, not by order
    PCG::ProgramGraph graph;
    auto& y = _Add(graph, ID::InputY);
    auto& x = _Add(graph, ID::InputX);
    auto& times = _Add(graph, ID::Multiply);
    auto& plus = _Add(graph, ID::Add);
    auto& out = _Add(graph, ID::OutputHeight);
    times.default_inputs[1] = 2.0f;
    graph.Connect({y.id, 0}, {times.id, 0});
    graph.Connect({times.id, 0}, {plus.id, 0});
    graph.Connect({x.id, 0}, {plus.id, 1});
    graph.Connect({plus.id, 0}, {out.id, 0});
    assert(_Height(graph, {3.0f, 4.0f}) == 11.0f);
  }
  {
    // Params read back whatever padding they took: (x - 1)^3 remapped
    PCG::ProgramGraph graph;
    auto& x = _Add(graph, ID::InputX);
    auto& minus = _Add(graph, ID::Subtract);
    auto& cube = _Add(graph, ID::PowerInt);
    auto& remap = _Add(graph, ID::Remap);
    auto& out = _Add(graph, ID::OutputHeight);
    minus.default_inputs[1] = 1.0f;
    cube.params[0] = 3;
    remap.params = {0.0f, 8.0f, 0.0f, 1.0f};
    graph.Connect({x.id, 0}, {minus.id, 0});
    graph.Connect({minus.id, 0}, {cube.id, 0});
    graph.Connect({cube.id, 0}, {remap.id, 0});
    graph.Connect({remap.id, 0}, {out.id, 0});
    assert(_Height(graph, {3.0f, 0.0f}) == 1.0f);
    // An unlinked chain folds to a constant at compile time
    graph.Disconnect({x.id, 0}, {minus.id, 0});
    minus.default_inputs[0] = 2.0f;
    assert(_Height(graph, {9.0f, 0.0f}) == 0.125f);
    // A param a node rejects is the compile error, on that node
    remap.params[1] = 0.0f;
    PCG::GraphRuntime runtime;
    const PCG::CompilationResult result = runtime.Compile(graph);
    assert(!result.success && result.node_id == static_cast<int>(remap.id));
  }
  {
    // An unlinked output is its default; noise with unlinked inputs folds
    PCG::ProgramGraph graph;
    auto& out = _Add(graph, ID::OutputHeight);
    out.default_inputs[0] = 7.0f;
    assert(_Height(graph, {}) == 7.0f);
    auto& noise = _Add(graph, ID::FN2_Simplex2D);
    graph.Connect({noise.id, 0}, {out.id, 0});
    const float folded = _Height(graph, {});
    assert(!std::isnan(folded) && folded == _Height(graph, {5.0f, 5.0f}));
  }
  {
    // No NaN from a zero divisor
    PCG::ProgramGraph graph;
    auto& x = _Add(graph, ID::InputX);
    auto& divide = _Add(graph, ID::Divide);
    auto& out = _Add(graph, ID::OutputHeight);
    divide.default_inputs[0] = 1.0f;
    graph.Connect({x.id, 0}, {divide.id, 1});
    graph.Connect({divide.id, 0}, {out.id, 0});
    assert(_Height(graph, {0.0f, 0.0f}) == 0.0f);
    assert(_Height(graph, {4.0f, 0.0f}) == 0.25f);
  }
  {
    // A graph volume's terrain: the height x + 2y over its bounds, up y
    PCG::GeneratorGraph generator;
    PCG::ProgramGraph& graph = *generator.GetGraph();
    auto& x = _Add(graph, ID::InputX);
    auto& y = _Add(graph, ID::InputY);
    auto& twice = _Add(graph, ID::Multiply);
    auto& plus = _Add(graph, ID::Add);
    auto& out = _Add(graph, ID::OutputHeight);
    twice.default_inputs[1] = 2.0f;
    graph.Connect({y.id, 0}, {twice.id, 0});
    graph.Connect({x.id, 0}, {plus.id, 0});
    graph.Connect({twice.id, 0}, {plus.id, 1});
    graph.Connect({plus.id, 0}, {out.id, 0});
    assert(generator.Compile().success);
    const Geometry::MeshData mesh = PCG::GraphVolumes::Heightfield(
        generator, {0.0f, 10.0f}, {4.0f, 14.0f}, 5);
    assert(mesh.vertices.size() == 25 && mesh.indices.size() == 16 * 6);
    assert(mesh.vertices[6].position == glm::vec3(1.0f, 23.0f, 11.0f));
    assert(mesh.vertices.back().position == glm::vec3(4.0f, 32.0f, 14.0f));
    for (const auto& vertex : mesh.vertices)
      assert(vertex.normal.y > 0.0f);
    // Nothing from a graph that doesn't compile
    PCG::GeneratorGraph empty;
    assert(PCG::GraphVolumes::Heightfield(empty, {}, {1.0f, 1.0f}, 4)
               .vertices.empty());
  }

  std::cout << "Graph node tests passed\n";
}

}  // namespace Tests

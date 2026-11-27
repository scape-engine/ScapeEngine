#ifndef GENERATOR_GRAPH_H
#define GENERATOR_GRAPH_H

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../core/sample.h"
#include "../generator_base.h"
#include "graph_runtime.h"
#include "program_graph.h"

/*******************************************************************************
 * GENERATOR GRAPH
 * TODO:
 * - Output index extraction during compile (needs iteration over output nodes)
 * - Batch processing in GenerateSeries (currently loops single queries)
 * - Implement range analysis for optimization
 * - Create a Cache struct
 ******************************************************************************/
namespace PCG {

class NodeTypeDB;

class GeneratorGraph : public FieldGenerator {
public:
  GeneratorGraph();
  ~GeneratorGraph();

  // Graph editing
  void Clear();

  // Its graph asset (.pcggraph), project-relative; empty: none
  std::string graph_path;
  // Reads the graph asset at `file` and compiles it; false (and an empty
  // graph) when it can't be read
  bool Load(const std::string& file);

  //
  // Graph access
  //
  ProgramGraph* GetGraph() override { return &graph_; }
  const ProgramGraph* GetGraph() const override { return &graph_; }

  //
  // Compilation
  //
  CompilationResult Compile();
  bool IsCompiled() const;

  //
  // Generation
  //

  // Single point query
  Sample GenerateSingle(glm::vec2 position) const;

  // Batch query
  void GenerateSeries(const std::vector<glm::vec2>& positions,
                      std::vector<Sample>& out_samples) const;

  // Grid query for heightmap generation
  void GenerateGrid(glm::vec2 origin, glm::vec2 size, glm::ivec2 resolution,
                    std::vector<Sample>& out_samples) const;

  //
  // Generator Interface
  //
  GeneratorType GetType() const override { return GeneratorType::Graph; }
  std::string GetDisplayName() const override { return "Generator Graph"; }
  Sample Eval(float x, float y) const override;

  // Clone for thread-local usage
  std::unique_ptr<Generator> Clone() const override;

private:
  // Cache for runtime state
  struct Cache {
    GraphRuntime::State state;
  };
  static Cache& GetTLSCache();

  // Wrapper around GraphRuntime for metadata
  struct Runtime {
    GraphRuntime runtime;

    // Output buffer indices
    int height_output_index = -1;
    int slope_output_index = -1;
  };

  ProgramGraph graph_;
  std::shared_ptr<Runtime> runtime_;
  mutable std::shared_mutex runtime_mutex_;
};
}  // namespace PCG

#endif  // GENERATOR_GRAPH_H
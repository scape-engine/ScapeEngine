#include "generator_graph.h"

#include "graph_serializer.h"
#include "node_types.h"

namespace PCG {

GeneratorGraph::GeneratorGraph() = default;

GeneratorGraph::~GeneratorGraph() {
  Clear();
}

GeneratorGraph::Cache& GeneratorGraph::GetTLSCache() {
  thread_local Cache cache;
  return cache;
}

void GeneratorGraph::Clear() {
  graph_.Clear();

  std::unique_lock lock(runtime_mutex_);
  runtime_.reset();
}

bool GeneratorGraph::Load(const std::string& file) {
  Clear();
  if (!GraphSerializer::LoadFromFile(file, graph_)) {
    graph_.Clear();
    return false;
  }
  Compile();
  return true;
}

CompilationResult GeneratorGraph::Compile() {
  auto new_runtime = std::make_shared<Runtime>();

  CompilationResult result = new_runtime->runtime.Compile(graph_);
  if (!result.success) {
    return result;
  }

  // TODO: Extract output indices from compiled runtime
  // This requires iterating output nodes and mapping to buffer addresses
  // ! For now, we assume first output as height
  new_runtime->height_output_index = 0;

  std::unique_lock lock(runtime_mutex_);
  runtime_ = new_runtime;

  return result;
}

bool GeneratorGraph::IsCompiled() const {
  std::shared_lock lock(runtime_mutex_);
  return runtime_ != nullptr;
}

Sample GeneratorGraph::GenerateSingle(glm::vec2 position) const {
  std::shared_ptr<Runtime> runtime_ptr;
  {
    std::shared_lock lock(runtime_mutex_);
    runtime_ptr = runtime_;
  }

  Sample sample;
  if (!runtime_ptr) {
    return sample;
  }

  Cache& cache = GetTLSCache();
  runtime_ptr->runtime.PrepareState(cache.state, 1);
  runtime_ptr->runtime.GenerateSingle(cache.state, position, sample);

  return sample;
}

void GeneratorGraph::GenerateSeries(const std::vector<glm::vec2>& positions,
                                    std::vector<Sample>& out_samples) const {
  std::shared_ptr<Runtime> runtime_ptr;
  {
    std::shared_lock lock(runtime_mutex_);
    runtime_ptr = runtime_;
  }

  out_samples.resize(positions.size());
  if (!runtime_ptr || positions.empty()) {
    return;
  }

  Cache& cache = GetTLSCache();
  runtime_ptr->runtime.PrepareState(cache.state, 1);

  // Generate each point (future: batch processing)
  for (size_t i = 0; i < positions.size(); ++i) {
    runtime_ptr->runtime.GenerateSingle(cache.state, positions[i],
                                        out_samples[i]);
  }
}

void GeneratorGraph::GenerateGrid(glm::vec2 origin, glm::vec2 size,
                                  glm::ivec2 resolution,
                                  std::vector<Sample>& out_samples) const {
  std::shared_ptr<Runtime> runtime_ptr;
  {
    std::shared_lock lock(runtime_mutex_);
    runtime_ptr = runtime_;
  }

  const size_t total_samples = resolution.x * resolution.y;
  out_samples.resize(total_samples);

  if (!runtime_ptr || total_samples == 0) {
    return;
  }

  Cache& cache = GetTLSCache();
  runtime_ptr->runtime.PrepareState(cache.state, 1);

  const glm::vec2 step = size / glm::vec2(resolution - 1);

  size_t index = 0;
  for (int y = 0; y < resolution.y; ++y) {
    for (int x = 0; x < resolution.x; ++x) {
      glm::vec2 pos = origin + glm::vec2(x, y) * step;
      runtime_ptr->runtime.GenerateSingle(cache.state, pos, out_samples[index]);
      ++index;
    }
  }
}

Sample GeneratorGraph::Eval(float x, float y) const {
  return GenerateSingle({x, y});
}

std::unique_ptr<Generator> GeneratorGraph::Clone() const {
  auto clone = std::make_unique<GeneratorGraph>();
  clone->graph_path = graph_path;

  // A deep copy: the graph owns its nodes
  clone->graph_.CopyFrom(graph_);

  // Recompile for the clone - each instance needs its own runtime state
  clone->Compile();

  return clone;
}

}  // namespace PCG
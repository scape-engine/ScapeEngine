#ifndef PROCMODEL_RESOURCE_H
#define PROCMODEL_RESOURCE_H

#include <string>

#include "engine/memory/resource.h"

#include "engine/pcg/pipeline/linear_pipeline.h"
#include "engine/pcg/pipeline/operation_registry.h"
#include "engine/pcg/pipeline/pipeline_descriptor.h"

#include "engine/pcg/procmodel/descriptor/model_descriptor.h"
#include "engine/pcg/procmodel/generator/resolved_model.h"
#include "engine/pcg/procmodel/model_graph/model_graph.h"

namespace ProcModel {

// Wrap archetype pipeline state
class ProcModelResource : public Resource {
public:
  ProcModelResource() = default;

  // Set the descriptor to load, and the operations its pipeline compiles to
  void SetSource(const std::string& descriptor_path,
                 const PCG::OperationRegistry& registry) {
    descriptor_path_ = descriptor_path;
    registry_ = &registry;
  }

  // Resource pipeline for async loading: descriptor parsing, model import,
  // graph building, resolving and pipeline compilation on the resource
  // worker, then mesh upload on the render thread. Resolved once done
  ResourcePipe Create() {
    return std::move(Pipe() >> BIND_TASK(ProcModelResource, LoadData) >>
                     BIND_TASK_WITH_FLAGS(ProcModelResource, UploadMeshes,
                                          TaskFlags::UseRenderThread));
  }

  ModelGraph& GetGraph() { return graph_; }
  const ModelGraph& GetGraph() const { return graph_; }

  ModelDescriptor& GetDescriptor() { return descriptor_; }
  const ModelDescriptor& GetDescriptor() const { return descriptor_; }

  PCG::PipelineDescriptor& GetPipelineDescriptor() {
    return pipeline_descriptor_;
  }
  const PCG::PipelineDescriptor& GetPipelineDescriptor() const {
    return pipeline_descriptor_;
  }

  PCG::LinearPipeline& GetPipeline() { return pipeline_; }
  const PCG::LinearPipeline& GetPipeline() const { return pipeline_; }

  bool IsResolved() const { return resolved_; }
  void SetResolved(bool value) { resolved_ = value; }

  void Destroy() override {
    graph_ = {};
    descriptor_ = {};
    pipeline_descriptor_ = {};
    pipeline_ = {};
    resolved_ = false;
  }

  std::string GetName() const { return descriptor_.model_id; }

private:
  bool LoadData();
  bool UploadMeshes();

  std::string descriptor_path_;
  const PCG::OperationRegistry* registry_ = nullptr;

  ModelGraph graph_;
  ModelDescriptor descriptor_;
  bool resolved_ = false;

  PCG::PipelineDescriptor pipeline_descriptor_;
  PCG::LinearPipeline pipeline_;
};

}  // namespace ProcModel

#endif  // PROCMODEL_RESOURCE_H

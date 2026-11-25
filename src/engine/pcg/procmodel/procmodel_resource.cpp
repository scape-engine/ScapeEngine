#include "procmodel_resource.h"

#include "engine/content/io/json.h"
#include "engine/content/loaders/mesh_loader.h"
#include "engine/core/logger.h"

#include "engine/pcg/pipeline/pipeline_descriptor_parser.h"

#include "engine/pcg/procmodel/descriptor/model_descriptor_parser.h"
#include "engine/pcg/procmodel/generator/descriptor_resolver.h"
#include "engine/pcg/procmodel/model_graph/model_graph_builder.h"

namespace ProcModel {

static bool _Fail(const std::string& message) {
  Logger::getInstance().Log(LogLevel::Error,
                            "[ProcModelResource] " + message);
  return false;
}

bool ProcModelResource::LoadData() {
  auto json_opt = Content::ReadJsonFile(descriptor_path_);
  if (!json_opt)
    return _Fail("Failed to read descriptor: " + descriptor_path_);

  if (!ModelDescriptorParser::FromJson(*json_opt, descriptor_))
    return _Fail("Failed to parse descriptor: " + descriptor_path_);

  PCG::ParsePipelineDescriptor(*json_opt, pipeline_descriptor_);

  Content::SceneData scene = Content::MeshLoader::LoadScene(descriptor_.path);
  if (scene.mesh_data.empty())
    return _Fail("Failed to load scene: " + descriptor_.path);

  graph_ = ModelGraphBuilder::Build(scene, descriptor_.path);

  if (!DescriptorResolver::Resolve(graph_, descriptor_).success)
    return _Fail("Descriptor resolution failed: " + descriptor_path_);

  std::vector<std::string> errors;
  auto compiled =
      PCG::LinearPipeline::Compile(pipeline_descriptor_, *registry_, errors);
  Logger::getInstance().Log(
      LogLevel::Info,
      "[ProcModelResource] Pipeline compiled with " +
          std::to_string(pipeline_descriptor_.entries.size()) + " entries");
  if (!compiled) {
    for (const auto& err : errors)
      _Fail("Pipeline compilation: " + err);
    return false;
  }
  pipeline_ = std::move(*compiled);
  return true;
}

bool ProcModelResource::UploadMeshes() {
  if (!graph_.UploadMeshes())
    return _Fail("Failed to upload meshes: " + descriptor_.path);
  resolved_ = true;
  return true;
}

}  // namespace ProcModel

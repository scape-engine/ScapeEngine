#include "procmodel.h"

#include <filesystem>

#include "engine/core/logger.h"

#include "engine/content/io/json.h"
#include "engine/content/loaders/mesh_loader.h"
#include "engine/context/engine_context.h"

#include "engine/pcg/pipeline/linear_pipeline.h"
#include "engine/pcg/pipeline/pipeline_descriptor_parser.h"

#include "engine/pcg/procmodel/descriptor/model_descriptor_parser.h"
#include "engine/pcg/procmodel/generator/descriptor_resolver.h"
#include "engine/pcg/procmodel/generator/model_generator.h"
#include "engine/pcg/procmodel/model_graph/model_graph_builder.h"
#include "engine/pcg/procmodel/procmodel_resource.h"
#include "engine/pcg/procmodel/validation/procmodel_evaluation.h"

namespace ProcModel {

Subsystem::Subsystem() = default;

void Subsystem::Init() {
  RegisterModelOperations(operation_registry_);

  Logger::getInstance().Log(
      LogLevel::Info,
      "[ProcModel::Subsystem] Init complete, registered " +
          std::to_string(
              operation_registry_.IsRegistered("vertex_deform") +
              operation_registry_.IsRegistered("duplicate_instances")) +
          " operations");
}

ResourceID Subsystem::LoadArchetype(const std::string& descriptor_path) {
  auto cache_it = procmodel_cache_.find(descriptor_path);
  if (cache_it != procmodel_cache_.end())
    return cache_it->second;

  auto& resource_mgr = EngineContext::resourceManager();

  auto [id, resource] =
      resource_mgr.Create<ProcModel::ProcModelResource>(descriptor_path);

  auto json_opt = Content::ReadJsonFile(descriptor_path);
  if (!json_opt) {
    resource_mgr.Release(id);
    return 0;
  }

  if (!ProcModel::ModelDescriptorParser::FromJson(*json_opt,
                                                  resource->GetDescriptor())) {
    Logger::getInstance().Log(
        LogLevel::Error, "[ProcModel::Subsystem] Failed to parse descriptor: " +
                             descriptor_path);
    resource_mgr.Release(id);
    return 0;
  }

  // A relative model path is beside the descriptor (else as given)
  {
    std::string& model_path = resource->GetDescriptor().path;
    const std::filesystem::path beside =
        std::filesystem::path(descriptor_path).parent_path() / model_path;
    if (std::filesystem::path(model_path).is_relative() &&
        std::filesystem::exists(beside))
      model_path = beside.string();
  }

  PCG::ParsePipelineDescriptor(*json_opt, resource->GetPipelineDescriptor());

  const auto& descriptor = resource->GetDescriptor();
  Content::SceneData scene = Content::MeshLoader::LoadScene(descriptor.path);
  if (scene.mesh_data.empty()) {
    Logger::getInstance().Log(
        LogLevel::Error,
        "[ProcModel::Subsystem] Failed to load scene: " + descriptor.path);
    resource_mgr.Release(id);
    return 0;
  }

  resource->GetGraph() =
      ProcModel::ModelGraphBuilder::Build(scene, descriptor.path);

  if (!resource->GetGraph().UploadMeshes()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ProcModel::Subsystem] Failed to upload meshes");
    resource_mgr.Release(id);
    return 0;
  }

  auto resolve_result = ProcModel::DescriptorResolver::Resolve(
      resource->GetGraph(), resource->GetDescriptor());
  if (!resolve_result.success) {
    Logger::getInstance().Log(
        LogLevel::Error, "[ProcModel::Subsystem] Descriptor resolution failed");
    resource_mgr.Release(id);
    return 0;
  }

  {
    std::vector<std::string> errors;
    auto compiled = PCG::LinearPipeline::Compile(
        resource->GetPipelineDescriptor(), operation_registry_, errors);
    Logger::getInstance().Log(
        LogLevel::Info,
        "[ProcModel::Subsystem] Pipeline compiled with " +
            std::to_string(resource->GetPipelineDescriptor().entries.size()) +
            " entries");
    if (!compiled) {
      for (const auto& err : errors) {
        Logger::getInstance().Log(
            LogLevel::Error,
            "[ProcModel::Subsystem] Pipeline compilation: " + err);
      }
      resource_mgr.Release(id);
      return 0;
    }
    resource->GetPipeline() = std::move(*compiled);
  }

  resource->SetResolved(true);
  procmodel_cache_[descriptor_path] = id;
  return id;
}

std::optional<ProcModel::InstanceData> Subsystem::GenerateInstance(
    const std::string& descriptor_path, uint64_t seed) {
  ResourceID resource_id = LoadArchetype(descriptor_path);
  if (resource_id == 0)
    return std::nullopt;

  auto& resource_mgr = EngineContext::resourceManager();
  auto resource =
      resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(resource_id);
  if (!resource || !resource->IsResolved()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ProcModel::Subsystem] Resource not ready");
    return std::nullopt;
  }

  std::vector<ProcModelSample> attempt_samples;
  auto output = ProcModel::ModelGenerator::Generate(
      resource->GetGraph(), resource->GetDescriptor(), resource->GetPipeline(),
      operation_registry_, seed, 10, &attempt_samples);

  // Route samples to log + ERA buffer
  for (const auto& s : attempt_samples) {
    ValidationLog().Write(s);
  }
  samples_.insert(samples_.end(),
                  std::make_move_iterator(attempt_samples.begin()),
                  std::make_move_iterator(attempt_samples.end()));

  if (!output) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "[ProcModel::Subsystem] Generation failed after "
                              "max retries for seed: " +
                                  std::to_string(seed));
    return std::nullopt;
  }

  return output;
}

// TODO: move load/build/resolve steps into a background ResourcePipe task,
// and the GPU upload into a UseRenderThread task. This will be needed
// so the pipeline doesn't block the frame
// TODO: Instantiate() currently ignores InstanceData::instance_geometry,
// so ECS-instantiated entities render undeformed source meshes.
// We need to wire instance_geometry through Instantiate when scene placement
// UX is needed. No need to refactor Editor preview path
ProcModel::ModelInstantiator::InstantiateResult Subsystem::SpawnInstance(
    const std::string& descriptor_path, uint64_t seed, Entity parent) {

  ResourceID resource_id = LoadArchetype(descriptor_path);
  if (resource_id == 0)
    return {};

  auto& resource_mgr = EngineContext::resourceManager();
  auto resource =
      resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(resource_id);
  if (!resource || !resource->IsResolved()) {
    Logger::getInstance().Log(LogLevel::Error,
                              "[ProcModel::Subsystem] Resource not ready");
    return {};
  }

  // Generate instance
  std::vector<ProcModelSample> attempt_samples;
  auto resolved = ProcModel::ModelGenerator::Generate(
      resource->GetGraph(), resource->GetDescriptor(), resource->GetPipeline(),
      operation_registry_, seed, 10, &attempt_samples);

  // Log every sample produced by this request, accumulate for ERA
  for (const auto& s : attempt_samples) {
    ValidationLog().Write(s);
  }
  samples_.insert(samples_.end(),
                  std::make_move_iterator(attempt_samples.begin()),
                  std::make_move_iterator(attempt_samples.end()));

  if (!resolved) {
    Logger::getInstance().Log(LogLevel::Warning,
                              "[ProcModel::Subsystem] Generation failed after "
                              "max retries for seed: " +
                                  std::to_string(seed));
    return {};
  }

  // Instantiate into ECS
  return ProcModel::ModelInstantiator::Instantiate(
      resolved->model, resource->GetGraph(), parent);
}

// Validation logger (one per session)
ValidationLogger& Subsystem::ValidationLog() {
  if (!validation_logger_)
    validation_logger_ = std::make_unique<ValidationLogger>();
  return *validation_logger_;
}

void Subsystem::ResetSamples() {
  samples_.clear();
  if (validation_logger_) {
    validation_logger_.reset();
  }
}

void Subsystem::Shutdown() {

  // Generate ERA metrics at shutdown
  // Only captures metrics for the current session
  // ! Should call one function instead of exposing the log logic here
  if (!validation_logger_)
    return;

  const auto& samples = samples_;
  if (samples.empty())
    return;

  ProcModelERA era = ProcModelEval::Compute(samples);

  std::string report_path = (std::filesystem::current_path() / "logs" /
                             "procmodel" / "era_metric.json")
                                .string();

  if (!ProcModelEval::WriteReport(era, report_path)) {
    Logger::getInstance().Log(
        LogLevel::Error, "[ProcModel::Subsystem] Failed to write ERA report");
  } else {
    Logger::getInstance().Log(
        LogLevel::Info,
        "[ProcModel::Subsystem] ERA report written to " + report_path);
  }
}

}  // namespace ProcModel
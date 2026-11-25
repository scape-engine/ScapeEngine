#include "procmodel.h"

#include <filesystem>

#include "engine/core/logger.h"

#include "engine/context/engine_context.h"
#include "engine/ecs/world.h"


#include "engine/pcg/procmodel/generator/model_generator.h"
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
  auto& resource_mgr = EngineContext::resourceManager();

  auto cache_it = procmodel_cache_.find(descriptor_path);
  if (cache_it != procmodel_cache_.end()) {
    // A failed load is retried: the descriptor may have been fixed since
    auto cached = resource_mgr.GetResource(cache_it->second);
    if (cached && cached->GetState() != ResourceState::FAILED)
      return cache_it->second;
    procmodel_cache_.erase(cache_it);
  }

  // Named after the file: the status bar shows "Loading <name>"
  auto [id, resource] = resource_mgr.Create<ProcModel::ProcModelResource>(
      std::filesystem::path(descriptor_path).filename().string());
  resource->SetSource(descriptor_path, operation_registry_);
  resource_mgr.Execute(resource->Create());  // sets FAILED if it can't queue

  procmodel_cache_[descriptor_path] = id;
  return id;
}

// True if a mesh renderer draws one of the graph's GPU meshes (entities
// created by SpawnInstance point at them)
static bool _MeshesInUse(const ModelGraph& graph) {
  for (auto [entity, renderer] :
       ECS::Main().View<MeshRendererComponent>().each()) {
    for (const auto& mesh : graph.gpu_meshes) {
      if (renderer.mesh_ == mesh.get())
        return true;
    }
  }
  return false;
}

ResourceID Subsystem::ReloadArchetype(const std::string& descriptor_path) {
  auto it = procmodel_cache_.find(descriptor_path);
  if (it != procmodel_cache_.end()) {
    auto& resource_mgr = EngineContext::resourceManager();
    auto previous =
        resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(it->second);

    // Released unless the worker is still loading it, or spawned entities
    // still draw its meshes
    // ponytail: a kept one stays until shutdown, refcount the meshes if
    // spawning from edited archetypes becomes common
    const bool loading =
        previous && (previous->GetState() == ResourceState::QUEUED ||
                     previous->GetState() == ResourceState::LOADING);
    if (previous && !loading && !_MeshesInUse(previous->GetGraph()))
      resource_mgr.Release(it->second);
    procmodel_cache_.erase(it);
  }
  return LoadArchetype(descriptor_path);
}

std::optional<ProcModel::InstanceData> Subsystem::GenerateInstance(
    ResourceID archetype, uint64_t seed) {
  auto& resource_mgr = EngineContext::resourceManager();
  auto resource =
      resource_mgr.GetResourceAs<ProcModel::ProcModelResource>(archetype);
  if (!resource || !resource->IsResolved())
    return std::nullopt;  // still loading in the background, or failed

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
    Logger::getInstance().Log(
        LogLevel::Warning,
        "[ProcModel::Subsystem] Archetype still loading: " + descriptor_path);
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